# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import gc
import os
import subprocess
import sys
import sysconfig
import threading
import time
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QCoreApplication, QEvent, QObject  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402

BROKEN = "the binding's parent/child graph broke"


def child_tail(output, lines=20):
    '''The last lines of a child process, for a failure message.'''
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    return "\n".join((output or "").splitlines()[-lines:])


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class StateLockTest(unittest.TestCase):
    """gc on a worker clears parents while main reparents the children."""

    def testGcOnWorkerAgainstReparent(self):
        for _ in range(3):
            # A fresh process: a broken graph crashes or hangs; a clean run takes
            # about a second
            try:
                proc = subprocess.run([sys.executable, __file__, "--stress"],
                                      capture_output=True, text=True, timeout=10)
            except subprocess.TimeoutExpired:
                self.fail(f"{BROKEN}: the run hangs")
            self.assertEqual(proc.returncode, 0,
                             f"{BROKEN}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("wrong 0", proc.stdout, f"{BROKEN}\n{child_tail(proc.stderr)}")


HAS_HOOKS = hasattr(Shiboken, "armFailpoint")


def run_child(test, option, broken):
    '''Run this file with `option` in a fresh process, expect "result ok".'''
    try:
        proc = subprocess.run([sys.executable, __file__, option],
                              capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired as e:
        test.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
    test.assertEqual(proc.returncode, 0,
                     f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
    test.assertIn("result ok", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")


def wait_reached(name, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if Shiboken.failpointReached(name):
            return True
        time.sleep(0.001)
    return False


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class ConstructorInstallTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()

    def child(self, option, broken):
        run_child(self, option, broken)

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testBaseInitFromTwoThreads(self):
        '''Two threads run the base __init__ of one object: one wins, one is refused.'''
        built = []

        class Counter:
            def __init__(self, *args, **kwargs):
                built.append(1)     # runs once per base __init__ that builds

        class Published(QObject, Counter):
            def __init__(self):
                pass            # the base __init__ comes from outside

        obj = Published()
        errors = []

        def init():
            try:
                QObject.__init__(obj)
            except RuntimeError as e:
                errors.append(str(e))

        self.assertTrue(Shiboken.armFailpoint("cptr-before-install"))
        parked = threading.Thread(target=init)
        parked.start()
        self.assertTrue(wait_reached("cptr-before-install"))
        init()
        Shiboken.releaseFailpoint("cptr-before-install")
        parked.join()
        self.assertEqual(len(errors), 1, "one object was initialized twice")
        self.assertIn("twice", errors[0])
        self.assertTrue(Shiboken.isValid(obj))
        self.assertEqual(len(built), 1, "the refused __init__ built an object")

    def testSecondInitRunsNoMixin(self):
        '''A second __init__ is refused before a mixin __init__ runs again.'''
        calls = []

        class Mixin:
            def __init__(self, *args, **kwargs):
                calls.append(1)
                super().__init__(*args, **kwargs)

        class Both(QObject, Mixin):
            pass

        obj = Both()
        with self.assertRaisesRegex(RuntimeError, "twice"):
            obj.__init__()
        self.assertEqual(len(calls), 1, "a refused __init__ ran the mixin __init__")
        self.assertTrue(Shiboken.isValid(obj))

    def testSecondInitWithTwoCppBases(self):
        '''Each base __init__ of two C++ bases is admitted once.'''
        from PySide6.QtCore import QRunnable
        built = []

        class Counter:
            def __init__(self, *args, **kwargs):
                built.append(1)

        class Multi(QObject, QRunnable, Counter):
            def __init__(self):
                pass

            def run(self):
                pass

        obj = Multi()
        QObject.__init__(obj)
        QRunnable.__init__(obj)
        good = len(built)
        for init in (QObject.__init__, QRunnable.__init__):
            with self.assertRaisesRegex(RuntimeError, "twice"):
                init(obj)
        self.assertEqual(len(built), good,
                         "a refused __init__ of one of two C++ bases built an object")
        self.assertTrue(Shiboken.isValid(obj))

    def testSecondInitRunsNoOverride(self):
        '''A second __init__ is refused before childEvent() sees a child.'''
        self.child("--init-child-event", "a refused __init__ ran a Python override")

    def testSecondInitKeepsApplication(self):
        '''A second QCoreApplication.__init__ is refused before building one.'''
        self.child("--init-application", "a refused __init__ replaced the application")

    def testFailedInitLeavesObjectFree(self):
        '''An __init__ failing early leaves the object free.'''
        self.child("--init-retry", "a failed __init__ kept the object")


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class WrapperLifetimeTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()

    def child(self, option, broken):
        run_child(self, option, broken)

    def testClassWithOtherCppBases(self):
        '''__class__ assignment to other C++ bases is refused.'''
        self.child("--class-assign", "a C++ base changed under a wrapper")

    def testContainerArgumentChanged(self):
        '''A list changed during appendRows(): C++ and the transfer see the same items.'''
        self.child("--container-argument", "a list argument changed during the call")

    def testContainerKeywordArgumentChanged(self):
        '''A keyword list changed during QJSValue.call() is refused or taken whole.'''
        self.child("--container-keyword", "a keyword list argument changed during the call")

    def testContainerArgumentKeptAsPassed(self):
        '''C++ keeps the caller's list, not the copy.'''
        from PySide6.QtStateMachine import QSignalTransition, QState
        transition = QSignalTransition()
        states = [QState(), QState()]
        before = sys.getrefcount(states)
        transition.setTargetStates(states)
        self.assertEqual(sys.getrefcount(states) - before, 1)
        states.clear()
        gc.collect()
        self.assertEqual(transition.targetStates(), [])

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testInvalidateListChanged(self):
        '''An object dropped from a list during invalidate() is not walked freed.'''
        self.child("--invalidate-list", "invalidate walked a freed object")


def _class_assign():
    from PySide6.QtCore import QRunnable

    class Single(QObject):
        pass

    class Other(QObject):
        pass

    class Multi(QObject, QRunnable):
        def run(self):
            pass

    obj = Single()
    obj.__class__ = Other               # the same C++ bases: allowed
    try:
        obj.__class__ = Multi
        refused = False
    except TypeError:
        refused = True
    Shiboken.delete(obj)
    del obj
    gc.collect()
    print(f"result {'ok' if refused else 'assigned'}", flush=True)


def _invalidate_list():
    from PySide6.QtCore import QPoint
    # Value types: invalidate() marks what has no C++ wrapper of its own
    keep = QPoint()
    victims = [QPoint(i, i) for i in range(50)] + [keep]

    def walk():
        Shiboken.invalidate(victims)

    assert Shiboken.armFailpoint("invalidate-after-collect")
    # Only the list owns the others: they go when it is cleared
    walker = threading.Thread(target=walk)
    walker.start()
    assert wait_reached("invalidate-after-collect")
    victims.clear()
    gc.collect()
    ballast = [QPoint() for _ in range(2000)]
    Shiboken.releaseFailpoint("invalidate-after-collect")
    walker.join()
    hit = sum(1 for b in ballast if not Shiboken.isValid(b))
    print(f"result {'ok' if hit == 0 and not Shiboken.isValid(keep) else f'hit {hit}'}",
          flush=True)


def _container_argument(seconds=3.0):
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtGui import QGuiApplication, QStandardItem
    app = QGuiApplication([])  # noqa: F841
    current = [[]]
    stop = threading.Event()

    # The list a call gets is the only owner of its items
    def replace():
        while not stop.is_set():
            items = current[0]
            items[:] = [QStandardItem() for _ in range(4)]
            if len(items) > 1:
                items.pop()

    changer = threading.Thread(target=replace)
    changer.start()
    wrong = 0
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        parent = QStandardItem()
        items = [QStandardItem() for _ in range(4)]
        current[0] = items
        parent.appendRows(items)
        for i in range(parent.rowCount()):
            if Shiboken.ownedByPython(parent.child(i)):
                wrong += 1
        del parent
    stop.set()
    changer.join()
    print(f"result {'ok' if wrong == 0 else f'wrong {wrong}'}", flush=True)


def _container_keyword(seconds=3.0):
    from PySide6.QtCore import QCoreApplication
    from PySide6.QtQml import QJSEngine, QJSValue
    app = QCoreApplication([])  # noqa: F841
    engine = QJSEngine()
    func = engine.evaluate("(function() { var s = 0;"
                           " for (var i = 0; i < arguments.length; ++i) s += arguments[i];"
                           " return s; })")
    good = [QJSValue(1) for _ in range(20)]
    items = list(good)
    stop = threading.Event()

    def swap():
        bad = object()
        while not stop.is_set():
            items[:] = [bad] * len(good)
            items[:] = good

    swapper = threading.Thread(target=swap)
    swapper.start()
    wrong = 0
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            result = func.call(args=items)
        except (TypeError, ValueError):
            continue
        if result.toInt() != len(good):
            wrong += 1
    stop.set()
    swapper.join()
    print(f"result {'ok' if wrong == 0 else f'wrong {wrong}'}", flush=True)


def _second_init(obj, *args):
    '''Whether obj.__init__(*args) was refused as a second __init__.'''
    try:
        obj.__init__(*args)
    except Exception as e:
        return isinstance(e, RuntimeError) and "twice" in str(e)
    return False


def _init_child_event():
    app = QCoreApplication([])  # noqa: F841
    events = []

    class Parent(QObject):
        def childEvent(self, event):
            events.append(event.type())

    class Probe(QObject):
        pass

    parent = Parent()
    obj = Probe()
    refused = _second_init(obj, parent)
    try:
        QObject()                   # no error of the refused __init__ left over
        clean = True
    except Exception:
        clean = False
    ok = refused and not events and clean and obj.parent() is None
    print(f"result {'ok' if ok else (refused, events, clean)}", flush=True)


def _init_application():
    app = QCoreApplication([])
    refused = _second_init(app, [])
    same = QCoreApplication.instance() is app
    print(f"result {'ok' if refused and same else (refused, same)}", flush=True)


def _init_retry():
    raising = [True]
    reached = []

    class Bare(QObject):
        def __init__(self):
            pass                    # the base __init__ comes from outside

    class Raising:
        def __init__(self, *args, **kwargs):
            if raising[0]:
                raise ValueError("mixin")

    class WithRaising(QObject, Raising):
        def __init__(self):
            pass

    class Counter:
        def __init__(self, *args, **kwargs):
            reached.append(1)

    class App(QCoreApplication, Counter):
        pass

    def fails(init, *args):
        try:
            init(*args)
        except Exception:
            return True
        return False

    bare, mixed, app = Bare(), WithRaising(), App.__new__(App)
    failed = [fails(QObject.__init__, bare, 1),    # the overload decision refuses it
              fails(QObject.__init__, mixed),
              # Not a list: the constructor code runs and builds no application
              fails(QCoreApplication.__init__, app, ("x",)) and reached == [1]]
    raising[0] = False
    QObject.__init__(bare)
    QObject.__init__(mixed)
    QCoreApplication.__init__(app, [])
    ok = (all(failed) and all(Shiboken.isValid(o) for o in (bare, mixed, app))
          and QCoreApplication.instance() is app)
    print(f"result {'ok' if ok else failed}", flush=True)


def _stress(rounds=200, parents=8, kids=40):
    app = QCoreApplication([])  # noqa: F841
    wrong = 0
    for _ in range(rounds):
        gc.disable()
        doomed = [QObject() for _ in range(parents)]
        others = [QObject() for _ in range(parents)]
        children = []
        for p in doomed:
            children.extend(QObject(p) for _ in range(kids))
            p.cycle = p
        del doomed, p
        barrier = threading.Barrier(2)

        def collect():
            barrier.wait()
            gc.collect()

        def reparent():
            barrier.wait()
            for i, child in enumerate(children):
                child.setParent(others[i % parents])

        threads = [threading.Thread(target=collect), threading.Thread(target=reparent)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        gc.enable()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        for i, child in enumerate(children):
            if not Shiboken.isValid(child) or child.parent() is not others[i % parents]:
                wrong += 1
    print(f"wrong {wrong}", flush=True)


if __name__ == '__main__':
    if "--class-assign" in sys.argv:
        _class_assign()
        sys.exit(0)
    if "--invalidate-list" in sys.argv:
        _invalidate_list()
        sys.exit(0)
    if "--container-argument" in sys.argv:
        _container_argument()
        sys.exit(0)
    if "--container-keyword" in sys.argv:
        _container_keyword()
        sys.exit(0)
    if "--init-child-event" in sys.argv:
        _init_child_event()
        sys.exit(0)
    if "--init-application" in sys.argv:
        _init_application()
        sys.exit(0)
    if "--init-retry" in sys.argv:
        _init_retry()
        sys.exit(0)
    if "--stress" in sys.argv:
        _stress()
        sys.exit(0)
    unittest.main()
