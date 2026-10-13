# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''QML construction and registration, against a second thread.'''

import gc
import os
import subprocess
import sys
import sysconfig
import tempfile
import threading
import time
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QCoreApplication, QObject, QUrl  # noqa: E402
from PySide6.QtQml import QmlElement, QQmlComponent, QQmlEngine, qmlTypeId  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")

QML_IMPORT_NAME = "FtQmlTest"
QML_IMPORT_MAJOR_VERSION = 1

STRESS_THREADS = 8
STRESS_ROUNDS = 100
STRESS_QML = b"import QtQml\nQtObject { property int value: 42 }\n"

entered = threading.Event()
other_creates = threading.Event()
nested = {}


def child_tail(output, lines=20):
    '''The last lines of a child process, for a failure message.'''
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    return "\n".join((output or "").splitlines()[-lines:])


def qml_create(engine, element):
    component = QQmlComponent(engine)
    component.setData(f"import QtQml\nimport FtQmlTest 1.0\n{element}".encode(), QUrl())
    return component.create()


@QmlElement
class Placed(QObject):
    '''Builds another QObject before its base constructor runs.'''

    def __init__(self, parent=None):
        self.inner = QObject()
        super().__init__(parent)
        self.setObjectName("placed")


@QmlElement
class Collector(QObject):
    '''The first one collects garbage while another thread constructs one.'''

    def __init__(self, parent=None):
        if not entered.is_set():
            entered.set()
            other_creates.wait(10)
            time.sleep(0.3)
            gc.collect()
        super().__init__(parent)
        self.setObjectName("collector")


@QmlElement
class Inner(QObject):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("inner")


@QmlElement
class Outer(QObject):
    '''Has QML construct an Inner before its base constructor runs.'''

    def __init__(self, parent=None):
        inner = qml_create(nested["engine"], "Inner { }")
        super().__init__(parent)
        self.setObjectName(f"outer+{inner.objectName() if inner else None}")


def wait_reached(name, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if Shiboken.failpointReached(name):
            return True
        time.sleep(0.001)
    return False


@unittest.skipUnless(FREE_THREADED, "free-threaded builds only")
class QmlFreeThreadedTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()

    def construct(self, option, expected, broken):
        '''A construction in a fresh process: a broken one hangs or crashes.'''
        try:
            proc = subprocess.run([sys.executable, __file__, option],
                                  capture_output=True, text=True, timeout=10)
        except subprocess.TimeoutExpired:
            self.fail(f"{broken}: the run hangs")
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn(f"result {expected}", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    def testParallelConstruction(self):
        '''QML constructions in two threads with gc do not deadlock.'''
        self.construct("--parallel", "collector collector",
                       "QML constructions in two threads deadlocked")

    def testNestedConstruction(self):
        '''A QML construction inside another one succeeds.'''
        self.construct("--nested", "outer+inner", "a nested QML construction failed")

    def testQObjectInInit(self):
        '''A QObject built in __init__() does not take the memory of QML.'''
        self.construct("--placed", "placed", "a QObject built in __init__ took QML's memory")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testImportNameRebound(self):
        '''QML_IMPORT_NAME rebound during registration is read safely.'''
        app = QCoreApplication.instance() or QCoreApplication([])  # noqa: F841 qmlTypeId()
        module = globals()
        module["QML_IMPORT_NAME"] = "".join(["FtQml", "Rebind"])

        def register():
            # type(): a class statement here makes a qualified name QML refuses.
            QmlElement(type("Rebound", (QObject,), {}))

        self.assertTrue(Shiboken.armFailpoint("qml-global-before-read"))
        registrar = threading.Thread(target=register)
        registrar.start()
        self.assertTrue(wait_reached("qml-global-before-read"))
        module["QML_IMPORT_NAME"] = "".join(["Other", "Import"])
        gc.collect()
        ballast = ["".join(["Ball", str(i)]) for i in range(2000)]  # noqa: F841
        Shiboken.releaseFailpoint("qml-global-before-read")
        registrar.join()
        module["QML_IMPORT_NAME"] = "FtQmlTest"
        self.assertGreaterEqual(qmlTypeId("FtQmlRebind", 1, 0, "Rebound"), 0,
                                "the QML import name was read from a freed string")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstAutoPropertiesFromTwoThreads(self):
        '''First auto_properties use in two threads publishes the helper once.'''
        self.construct("--helper-auto", "references 1",
                       "the helper module was published twice")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstComponentLoadFromTwoThreads(self):
        '''First load_qml_component() in two threads publishes the helper once.'''
        self.construct("--helper-component", "references 1",
                       "the helper module was published twice")

    def testFeaturesFromThreads(self):
        '''QtQmlFeatures used from threads gives exact results; the GIL stays off.'''
        broken = "QtQmlFeatures failed in threads"
        env = {k: v for k, v in os.environ.items() if k != "PYTHON_GIL"}
        try:
            proc = subprocess.run([sys.executable, __file__, "--stress"], env=env,
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired as e:
            self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("result errors 0 gil False", proc.stdout,
                      f"{broken}\n{child_tail(proc.stdout + proc.stderr)}")


def _parallel():
    app = QCoreApplication([])  # noqa: F841
    results = []

    def build(second):
        engine = QQmlEngine()
        component = QQmlComponent(engine)
        component.setData(b"import QtQml\nimport FtQmlTest 1.0\nCollector { }", QUrl())
        if second:
            other_creates.set()
        obj = component.create()
        results.append(obj.objectName() if obj else None)

    first = threading.Thread(target=build, args=(False,))
    first.start()
    entered.wait(10)
    second = threading.Thread(target=build, args=(True,))
    second.start()
    first.join()
    second.join()
    print(f"result {' '.join(map(str, results))}", flush=True)


def _nested():
    app = QCoreApplication([])  # noqa: F841
    nested["engine"] = QQmlEngine()
    obj = qml_create(nested["engine"], "Outer { }")
    print(f"result {obj.objectName() if obj else None}", flush=True)


def _placed():
    app = QCoreApplication([])  # noqa: F841
    engine = QQmlEngine()
    obj = qml_create(engine, "Placed { }")
    print(f"result {obj.objectName() if obj else None}", flush=True)


def _helper(kind):
    '''Racing first uses of a helper module; print the references it gained.'''
    import importlib
    from PySide6.QtQmlFeatures import auto_properties, load_qml_component
    name = "auto_property_helper" if kind == "auto" else "qml_component_helper"
    helper = importlib.import_module(f"PySide6.support.{name}")
    before = sys.getrefcount(helper)

    def use():
        if kind == "auto":
            auto_properties(type("Decorated", (QObject,), {}))
            return
        try:
            load_qml_component(None, "none.qml")
        except TypeError:
            pass                        # no engine; the helper is loaded first

    assert Shiboken.armFailpoint("qml-helper-before-publish")
    other = threading.Thread(target=use)
    other.start()
    assert wait_reached("qml-helper-before-publish")
    use()
    Shiboken.releaseFailpoint("qml-helper-before-publish")
    other.join()
    gc.collect()
    print(f"result references {sys.getrefcount(helper) - before}", flush=True)


def _stress():
    '''Workers run rounds of their own class while main loads QML.'''
    from PySide6.QtQmlFeatures import (auto_properties, computed, effect, load_qml_component,
                                       watch)
    app = QCoreApplication([])  # noqa: F841
    errors = []
    barrier = threading.Barrier(STRESS_THREADS + 1)

    def one_round(name):
        @auto_properties
        class Counter(QObject):
            def __init__(self):
                super().__init__()
                self.count = 0
                self.step = 1
                self._watched = []
                self._effects = 0

            @computed("count", "step")
            def total(self) -> int:
                return self.count * self.step

            @watch("count")
            def on_count(self, change):
                self._watched.append((change.old, change.new))

            @effect("count", "step")
            def on_any(self):
                self._effects += 1

        for obj in [Counter() for _ in range(3)]:
            for value in range(1, 6):
                obj.count = value
            obj.step = 2
            obj.count = 5                   # unchanged: no callback
            got = (obj._watched, obj._effects, obj.total)
            expected = ([(0, 1), (1, 2), (2, 3), (3, 4), (4, 5)], 6, 10)
            if got != expected:
                errors.append(f"{name}: {got} != {expected}")

    def worker(index):
        barrier.wait()
        for round_ in range(STRESS_ROUNDS):
            try:
                one_round(f"{index}/{round_}")
            except Exception as e:          # noqa: B036
                errors.append(f"{index}/{round_}: {e!r}")

    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "Stress.qml")
        with open(path, "wb") as f:
            f.write(STRESS_QML)
        engine = QQmlEngine()
        workers = [threading.Thread(target=worker, args=(i,)) for i in range(STRESS_THREADS)]
        for t in workers:
            t.start()
        start = time.monotonic()
        barrier.wait()
        loads = 0
        while any(t.is_alive() for t in workers):
            obj = load_qml_component(engine, path).create()
            if obj.value != 42:
                errors.append(f"load {loads}: value {obj.value}")
            loads += 1
        for t in workers:
            t.join()
        seconds = time.monotonic() - start
    print(f"rounds {STRESS_THREADS * STRESS_ROUNDS} threads {STRESS_THREADS} loads {loads} "
          f"seconds {seconds:.2f} errors {len(errors)} {errors[:3]}", flush=True)
    print(f"result errors {len(errors)} gil {sys._is_gil_enabled()}", flush=True)


if __name__ == '__main__':
    for option, run in (("--parallel", _parallel), ("--nested", _nested), ("--placed", _placed),
                        ("--helper-auto", lambda: _helper("auto")),
                        ("--helper-component", lambda: _helper("component")),
                        ("--stress", _stress)):
        if option in sys.argv:
            run()
            sys.exit(0)
    unittest.main()
