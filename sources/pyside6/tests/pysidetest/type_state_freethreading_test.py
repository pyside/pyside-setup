# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''State shared by all types, against a second thread.'''

import ctypes
import gc
import os
import subprocess
import sys
import sysconfig
import tempfile
import threading
import time
import types
import unittest
import weakref

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QCoreApplication, QObject, QTimer, Signal  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")

# Two bindings of their own, first imported in two threads
TWO_BINDINGS = """
import sys, threading
sys.path.insert(0, sys.argv[1])
from init_paths import init_all_test_paths
init_all_test_paths()
barrier = threading.Barrier(len(sys.argv) - 2)
errors = []

def load(name):
    barrier.wait()
    try:
        __import__(name)
    except BaseException as e:
        errors.append(repr(e))

threads = [threading.Thread(target=load, args=(n,)) for n in sys.argv[2:]]
for t in threads:
    t.start()
for t in threads:
    t.join()
print("result", "ok" if not errors else errors, flush=True)
"""


def child_tail(output, lines=20):
    '''The last lines of a child process, for a failure message.'''
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    return "\n".join((output or "").splitlines()[-lines:])


def wait_reached(name, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if Shiboken.failpointReached(name):
            return True
        time.sleep(0.001)
    return False


def graph_edges(path):
    with open(path) as f:
        return sorted(line for line in f if "->" in line)


@unittest.skipUnless(FREE_THREADED, "free-threaded builds only")
class TypeStateFreeThreadedTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testHierarchyWalkSeesOneGraph(self):
        '''A module import during a hierarchy walk: the walk sees one graph.'''
        base = tempfile.mkdtemp()
        before = os.path.join(base, "before.dot")
        during = os.path.join(base, "during.dot")
        Shiboken.dumpTypeGraph(before)
        self.assertTrue(Shiboken.armFailpoint("hierarchy-mid-traversal"))
        walker = threading.Thread(target=Shiboken.dumpTypeGraph, args=(during,))
        walker.start()
        self.assertTrue(wait_reached("hierarchy-mid-traversal"))
        import PySide6.QtWidgets  # noqa: F401  a few hundred edges
        Shiboken.releaseFailpoint("hierarchy-mid-traversal")
        walker.join()
        self.assertEqual(graph_edges(during), graph_edges(before), "the walk saw two graphs")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testDocPerThread(self):
        '''A help text in one thread does not change another's __doc__.'''
        expected = QTimer.__doc__
        self.assertTrue(Shiboken.armFailpoint("doc-before-helptext"))
        reader = threading.Thread(target=lambda: QObject.__doc__)
        reader.start()
        self.assertTrue(wait_reached("doc-before-helptext"))
        got = QTimer.__doc__
        Shiboken.releaseFailpoint("doc-before-helptext")
        reader.join()
        self.assertEqual(got, expected, "__doc__ came from the raw descriptor")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testMroWalkSurvivesBasesSwap(self):
        '''__bases__ assigned during an override lookup: the walk stays valid.'''
        from PySide6.QtCore import QRunnable, QThreadPool
        app = QCoreApplication.instance() or QCoreApplication([])  # noqa: F841
        ran = threading.Event()

        class Mid1(QRunnable):
            pass

        class Mid2(QRunnable):
            pass

        class Work(Mid1):
            def run(self):
                ran.set()

        # A walk over a freed tuple that meets a decoy takes run() for the default
        decoy = type("Decoy", (object,), {"run": Work.run})
        width = len(Work.__mro__)
        work = Work()
        work.setAutoDelete(False)
        pool = QThreadPool.globalInstance()
        self.assertTrue(Shiboken.armFailpoint("mro-after-snapshot"))
        pool.start(work)
        self.assertTrue(wait_reached("mro-after-snapshot"))
        Work.__bases__ = (Mid2,)
        gc.collect()            # the old mro is freed by the collector only
        ballast = [tuple([decoy] * width) for _ in range(2000)]  # noqa: F841
        Shiboken.releaseFailpoint("mro-after-snapshot")
        self.assertTrue(pool.waitForDone(30000))
        self.assertTrue(ran.is_set(), "the walk left the mro it started on")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testBasesWalkSurvivesBasesSwap(self):
        '''__bases__ assigned during a property lookup: the walk stays valid.'''
        from PySide6.QtCore import Property

        class Holder(QObject):
            @Property(int)
            def val(self):
                return 7

        class Other(QObject):
            pass

        class Sub(Holder):
            pass

        obj = Sub()
        answer = []
        decoy = type("Decoy", (object,), {})
        self.assertTrue(Shiboken.armFailpoint("bases-after-snapshot"))
        asker = threading.Thread(target=lambda: answer.append(obj.property("val")))
        asker.start()
        self.assertTrue(wait_reached("bases-after-snapshot"))
        Sub.__bases__ = (Other,)
        ballast = [(decoy,) for _ in range(2000)]  # noqa: F841
        Shiboken.releaseFailpoint("bases-after-snapshot")
        asker.join()
        self.assertEqual(answer, [7], "the walk left the bases it started on")

    def testPostRoutinesFromThreads(self):
        '''Post routines registered from several threads all run.'''
        proc = subprocess.run([sys.executable, __file__, "--post"],
                              capture_output=True, timeout=120)
        ran = proc.stdout.count(b"r")
        self.assertEqual((proc.returncode, ran), (0, 4000),
                         f"post routines lost\n{child_tail(proc.stderr)}")

    def testDictChangedDuringConversion(self):
        '''A dict changed during its conversion does not crash.'''
        obj = QObject()
        d = {f"k{i}": list(range(64)) for i in range(64)}
        stop = threading.Event()

        def mutate():
            i = 0
            while not stop.is_set():
                d[f"k{i % 64}"] = list(range(64))
                i += 1

        mutator = threading.Thread(target=mutate)
        mutator.start()
        try:
            for _ in range(5000):
                obj.setProperty("map", d)
        finally:
            stop.set()
            mutator.join()
        self.assertEqual(len(obj.property("map")), 64)

    def testMetaTypeReadDuringRegistration(self):
        '''Metatypes read during a QML registration keep their meta objects.'''
        from PySide6.QtCore import QMetaType
        from PySide6.QtQml import qmlRegisterSingletonType
        count = 3000            # several rehashes of the hash
        names = []
        wrong = []
        stop = threading.Event()

        def reader():
            while not stop.is_set():
                for name in list(names):
                    mo = QMetaType.metaObjectForType(QMetaType.fromName(f"{name}*".encode()).id())
                    if mo is None or mo.className() != name:
                        wrong.append(name)

        worker = threading.Thread(target=reader)
        worker.start()
        try:
            for i in range(count):
                name = f"TypeStateSingleton{i}"
                cls = type(name, (QObject,), {})
                qmlRegisterSingletonType(cls, "TypeStateTest", 1, 0, name)
                names.append(name)
        finally:
            stop.set()
            worker.join()
        self.assertEqual(wrong, [], "a metatype lost its meta object")

    def testConverterNamesDuringImport(self):
        '''Converter names added by imports and QEnums are found meanwhile.'''
        for _ in range(6):      # one run may pass by luck
            try:
                proc = subprocess.run([sys.executable, __file__, "--converters"],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired as e:
                self.fail(f"the converter registry broke: the run hangs\n{child_tail(e.stderr)}")
            self.assertEqual(proc.returncode, 0,
                             f"the converter registry broke: rc {proc.returncode}"
                             f"\n{child_tail(proc.stderr)}")
            self.assertIn("wrong 0", proc.stdout,
                          f"the converter registry broke\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testConversionsExtendedDuringImport(self):
        '''Conversions extended during a walk: the walk keeps its list.'''
        for _ in range(3):
            proc = subprocess.run([sys.executable, __file__, "--conversions"],
                                  capture_output=True, text=True, timeout=60)
            self.assertEqual(proc.returncode, 0,
                             f"a conversion walk lost its list: rc {proc.returncode}"
                             f"\n{child_tail(proc.stderr)}")
            self.assertIn("result ok", proc.stdout,
                          f"a conversion walk lost its list\n{child_tail(proc.stderr)}")

    def testParallelModuleExecs(self):
        '''Module execs at once while names are looked up (a guard).'''
        proc = subprocess.run([sys.executable, __file__, "--execs"],
                              capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0,
                         f"the module tables broke: rc {proc.returncode}"
                         f"\n{child_tail(proc.stderr)}")
        self.assertIn("wrong 0", proc.stdout,
                      f"the module tables broke\n{child_tail(proc.stderr)}")

    def testQEnumClassesFromThreads(self):
        '''QEnum classes in several threads each get exactly their own enum.'''
        for _ in range(6):      # one run may pass by luck
            try:
                proc = subprocess.run([sys.executable, __file__, "--qenums"],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired as e:
                self.fail(f"the QEnum registration broke: the run hangs\n{child_tail(e.stderr)}")
            self.assertEqual(proc.returncode, 0,
                             f"the QEnum registration broke: rc {proc.returncode}"
                             f"\n{child_tail(proc.stderr)}")
            self.assertIn("wrong 0", proc.stdout,
                          f"the QEnum registration broke\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstStreamFromTwoThreads(self):
        '''First QDataStream reads in two threads make one helper.'''
        proc = subprocess.run([sys.executable, __file__, "--stream-helper"],
                              capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0,
                         f"two stream helpers: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("helpers 1 ", proc.stdout, f"two stream helpers\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testDirDuringModuleExec(self):
        '''dir() of a module whose exec runs in another thread works.'''
        broken = "dir() met a module before its tables"
        proc = subprocess.run([sys.executable, __file__, "--module-dir"],
                              capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("result ok", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testImportWaitsForModuleExec(self):
        '''An import waits for an exec in another thread, not half initialized.'''
        broken = "a module was taken half initialized"
        try:
            proc = subprocess.run([sys.executable, __file__, "--import-waits"],
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired as e:
            self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("result ok", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    def testFirstImportsOfTwoBindings(self):
        '''First imports of two bindings at once initialize libshiboken once.'''
        broken = "two first imports hung"
        tests_dir = os.fspath(Path(__file__).resolve().parents[1])
        pairs = (["sample", "minimal"], ["sample", "shiboken6"], ["shiboken6", "sample"])
        for names in pairs * 2:
            try:
                proc = subprocess.run([sys.executable, "-c", TWO_BINDINGS, tests_dir, *names],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired as e:
                self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
            self.assertEqual(proc.returncode, 0,
                             f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("result ok", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testModuleSeenWhileItsDictIsReplaced(self):
        '''A module seen during replaceModuleDict() keeps its dict and class.'''
        module = types.ModuleType("probe")

        class Sub(types.ModuleType):
            pass

        class Dict(dict):
            pass

        name = "module-dict-between-stores"
        self.assertTrue(Shiboken.armFailpoint(name))
        replacer = threading.Thread(target=Shiboken.replaceModuleDict,
                                    args=(module, Sub, Dict(vars(module))))
        replacer.start()
        while replacer.is_alive() and not Shiboken.failpointReached(name):
            time.sleep(0.001)
        seen = (type(module), type(module.__dict__))
        Shiboken.releaseFailpoint(name)
        replacer.join()
        self.assertNotEqual(seen, (types.ModuleType, Dict),
                            "a module was seen between the two stores")

    def testGenericEnumAppendsFromThreads(self):
        '''QEnum enums appended from several threads: none is lost.'''
        broken = "the generic enum registry lost an append"
        # Without the lock 35 of 50 runs break (4 threads, 10 cores), so ten
        # runs miss far below 0.1%
        for _ in range(10):
            try:
                proc = subprocess.run([sys.executable, __file__, "--generic-enums"],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired as e:
                self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
            self.assertEqual(proc.returncode, 0,
                             f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("errors 0", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    def churnWhile(self, churn, work, count):
        '''work() count times while another thread keeps calling churn(i).'''
        stop = threading.Event()

        def loop():
            i = 0
            while not stop.is_set():
                churn(i)
                i += 1

        thread = threading.Thread(target=loop)
        thread.start()
        try:
            for _ in range(count):
                work()
        finally:
            stop.set()
            thread.join()

    @staticmethod
    def classChurn(cls):
        '''Attributes come and go: the class dict resizes, its values are freed.'''
        def churn(i):
            for n in range(64):
                setattr(cls, f"churn{n}", [i])
            for n in range(64):
                delattr(cls, f"churn{n}")
        return churn

    @staticmethod
    def dictChurn(d):
        '''Keys come and go in a dict of strings: it resizes, its values are freed.'''
        def churn(i):
            for n in range(64):
                d[f"churn{n}"] = f"value{i}"
            for n in range(64):
                del d[f"churn{n}"]
        return churn

    def testBaseClassChangedDuringMetaObjectBuild(self):
        '''A mixin changed while a subclass parses its dict does not crash.'''
        class Mixin:
            pass

        self.churnWhile(self.classChurn(Mixin),
                        lambda: type("Sub", (Mixin, QObject), {}), 3000)

    def testClassChangedDuringQObjectInit(self):
        '''A class changed while an instance looks for signals does not crash.'''
        class Sub(QObject):
            pass

        self.churnWhile(self.classChurn(Sub), Sub, 20000)

    def testClassChangedDuringEnumShortcutLookup(self):
        '''A class changed during a search for an old enum name does not crash.'''
        from PySide6.QtCore import QEvent

        self.churnWhile(self.classChurn(QEvent),
                        lambda: hasattr(QEvent, "NoSuchEnumShortcut"), 20000)

    def testFirstInstancesNameLateSignals(self):
        '''The first instances of a class, made at once, name its late signals once.'''
        for _ in range(5000):
            class Sub(QObject):
                pass

            Sub()
            for i in range(32):
                setattr(Sub, f"late_signal_with_a_long_name_{i}", Signal())
            barrier = threading.Barrier(8)

            def make():
                barrier.wait()
                Sub()

            threads = [threading.Thread(target=make) for _ in range(8)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
            self.assertEqual(str(Sub.late_signal_with_a_long_name_31),
                             "late_signal_with_a_long_name_31()")

    def testClassInfoDictChanged(self):
        '''A dict given to ClassInfo and changed meanwhile does not crash.'''
        from PySide6.QtCore import ClassInfo

        d = {f"k{i}": f"v{i}" for i in range(64)}
        self.churnWhile(self.dictChurn(d), lambda: ClassInfo(d), 20000)

    def testDebugFormatOfChangingDict(self):
        '''The debug formatters of a dict another thread changes do not crash.'''
        from init_paths import init_all_test_paths
        init_all_test_paths()
        from testbinding import formatDictForTest

        d = {f"k{i}": f"v{i}" for i in range(64)}
        self.churnWhile(self.dictChurn(d), lambda: formatDictForTest(d), 20000)

    def testModuleDictChangedDuringSignatures(self):
        '''A module dict changed while its signatures finish does not crash.'''
        # The crash is rare per process: a counter-proof gets more tries
        snapshot_on = Shiboken.ftOptionFlags() & 0x4000
        for _ in range(16 if snapshot_on else 64):
            proc = subprocess.run([sys.executable, __file__, "--module-dict"],
                                  capture_output=True, text=True, timeout=60)
            self.assertEqual(proc.returncode, 0,
                             f"crashed: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("result ok", proc.stdout, child_tail(proc.stderr))

    def testDictSubclassConversion(self):
        '''A dict subclass converts by its stored items, not its keys().'''
        from PySide6.QtCore import QJsonDocument

        class Stored(dict):
            def keys(self):
                raise RuntimeError("keys() consulted")

            def __iter__(self):
                raise RuntimeError("__iter__ consulted")

        doc = QJsonDocument(Stored(a=1, b=2))
        self.assertEqual(doc.object(), {"a": 1, "b": 2})


def _post_routines(threads=8, each=500):
    from PySide6.QtCore import QCoreApplication, qAddPostRoutine
    app = QCoreApplication([])  # noqa: F841
    barrier = threading.Barrier(threads)

    def register():
        barrier.wait()
        for _ in range(each):
            qAddPostRoutine(lambda: os.write(1, b"r"))

    workers = [threading.Thread(target=register) for _ in range(threads)]
    for t in workers:
        t.start()
    for t in workers:
        t.join()
    # The routines run when the application goes; each writes one "r"


def _converters():
    import importlib
    from enum import Enum
    from PySide6.QtCore import Property, QEnum

    class Holder(QObject):
        def __init__(self):
            super().__init__()
            self._text = ""

        def getText(self):
            return self._text

        def setText(self, text):
            self._text = text

        # Through the meta object: a converter looked up by type name
        text = Property(str, getText, setText)

    obj = Holder()
    names = ["QtGui", "QtWidgets", "QtNetwork", "QtQml", "QtQuick", "QtSql", "QtXml",
             "QtTest", "QtConcurrent", "QtOpenGL", "QtSvg", "QtPrintSupport", "QtDBus",
             "QtMultimedia", "QtPositioning", "QtWebSockets", "QtCharts", "QtStateMachine"]
    done = threading.Event()
    wrong = []

    def load():
        for name in names:
            try:
                importlib.import_module(f"PySide6.{name}")
            except ImportError:
                pass
        done.set()

    loader = threading.Thread(target=load)
    loader.start()
    namespace = {"QObject": QObject, "QEnum": QEnum, "Enum": Enum}
    i = 0
    while not done.is_set():
        i += 1
        # A class with a QEnum adds a converter name, as the imports do
        exec(f"class H{i}(QObject):\n    @QEnum\n    class E{i}(Enum):\n        A = 1\n",
             namespace)
        obj.setProperty("text", f"v{i}")
        if obj.property("text") != f"v{i}":
            wrong.append(i)
    loader.join()
    print(f"wrong {len(wrong)} of {i}", flush=True)


def _conversions():
    from PySide6.QtCore import QLineF, QPoint, QPointF
    assert "PySide6.QtGui" not in sys.modules
    line = QLineF()
    point = QPoint(1, 2)
    result = []

    def walk():
        line.setP1(point)       # QPoint to QPointF: a walk over QPointF's conversions
        result.append(line.p1())

    assert Shiboken.armFailpoint("conversions-mid-walk")
    walker = threading.Thread(target=walk)
    walker.start()
    assert wait_reached("conversions-mid-walk")
    import PySide6.QtGui  # noqa: F401  adds implicit conversions to QPointF
    # C++ objects of the old list's size reuse its freed buffer
    from PySide6.QtCore import QRectF
    ballast = [(QLineF(i, i, i, i), QRectF(i, i, i, i)) for i in range(20000)]  # noqa: F841
    Shiboken.releaseFailpoint("conversions-mid-walk")
    walker.join()
    ok = result == [QPointF(1, 2)]
    print(f"result {'ok' if ok else result}", flush=True)


def _execs():
    import importlib
    from PySide6.QtCore import QAbstractEventDispatcher, QCoreApplication, QThread
    app = QCoreApplication([])  # noqa: F841
    # Each depends on QtCore only, which is complete, so their execs overlap
    names = ["QtNetwork", "QtXml", "QtConcurrent", "QtDBus", "QtSerialPort", "QtTextToSpeech",
             "QtPositioning", "QtBluetooth", "QtNfc", "QtSensors"]
    barrier = threading.Barrier(len(names) + 1)
    errors = []
    stop = threading.Event()

    def load(name):
        barrier.wait()
        try:
            importlib.import_module(f"PySide6.{name}")
        except ImportError:
            pass
        except BaseException as e:      # noqa: B036
            errors.append(f"{name}: {e}")

    def lookups():
        # A new thread's dispatcher class is not exposed: an unknown converter name
        barrier.wait()
        while not stop.is_set():
            t = QThread()
            t.start()
            QAbstractEventDispatcher.instance(t)
            t.quit()
            t.wait()

    loaders = [threading.Thread(target=load, args=(n,)) for n in names]
    looker = threading.Thread(target=lookups)
    for t in loaders + [looker]:
        t.start()
    for t in loaders:
        t.join()
    stop.set()
    looker.join()
    print(f"wrong {len(errors)} {errors[:2]}", flush=True)


def _qenums(threads=16, each=1000):
    from enum import Enum
    from PySide6.QtCore import QEnum
    barrier = threading.Barrier(threads)
    made = {}
    errors = []

    def make(tag):
        namespace = {"QObject": QObject, "QEnum": QEnum, "Enum": Enum}
        barrier.wait()
        try:
            for i in range(each):
                name = f"{tag}_{i}"
                exec(f"class H{name}(QObject):\n"
                     f"    @QEnum\n"
                     f"    class E{name}(Enum):\n"
                     f"        A = {tag * 10000 + i}\n", namespace)
                made[name] = namespace[f"H{name}"]
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    makers = [threading.Thread(target=make, args=(t,)) for t in range(threads)]
    for t in makers:
        t.start()
    for t in makers:
        t.join()
    wrong = list(errors)
    if len(made) != threads * each:
        wrong.append(("made", len(made)))
    for name, cls in made.items():
        meta = cls.staticMetaObject
        own = [meta.enumerator(k).name() for k in range(meta.enumeratorOffset(),
                                                        meta.enumeratorCount())]
        enum = cls.__dict__.get(f"E{name}")
        # Exactly its own enum, with its own value
        if own != [f"E{name}"] or enum is None or enum.A.value != made_value(name):
            wrong.append(("enum", name, own))
    print(f"wrong {len(wrong)} of {len(made)} {wrong[:3]}", flush=True)


def _stream_helper():
    from PySide6.QtCore import QByteArray, QDataStream, QIODevice
    data = QByteArray()
    QDataStream(data, QIODevice.OpenModeFlag.WriteOnly).writeQVariant({1, 2})

    def read():
        return QDataStream(data, QIODevice.OpenModeFlag.ReadOnly).readQVariant()

    got = []
    assert Shiboken.armFailpoint("stream-helper-before-publish")
    reader = threading.Thread(target=lambda: got.append(read()))
    reader.start()
    assert wait_reached("stream-helper-before-publish")
    got.append(read())                  # builds and publishes its helper
    Shiboken.releaseFailpoint("stream-helper-before-publish")
    reader.join()
    gc.collect()
    helpers = sum(1 for o in gc.get_objects() if getattr(o, "__name__", None) == "_safe_loads")
    print(f"helpers {helpers} values {got}", flush=True)


def _module_dir():
    import importlib
    errors = []

    def load():
        try:
            importlib.import_module("PySide6.QtGui")
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    assert Shiboken.armFailpoint("module-dir-before-table")
    loader = threading.Thread(target=load)
    loader.start()
    assert wait_reached("module-dir-before-table")
    # The module is in sys.modules, its exec is not done
    names = dir(sys.modules["PySide6.QtGui"])
    Shiboken.releaseFailpoint("module-dir-before-table")
    loader.join()
    ok = not errors and isinstance(names, list)
    print(f"result {'ok' if ok else errors}", flush=True)


def _module_dict():
    import importlib
    errors = []

    def load():
        try:
            importlib.import_module("PySide6.QtGui")
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    # The module is in sys.modules during its exec, its signatures are finished at the end.
    # A failpoint holds the exec until the dict is prepared; without one it is a race.
    if HAS_HOOKS:
        assert Shiboken.armFailpoint("module-exec-before-types")
    loader = threading.Thread(target=load)
    loader.start()
    if HAS_HOOKS:
        assert wait_reached("module-exec-before-types")
    else:
        while loader.is_alive() and "PySide6.QtGui" not in sys.modules:
            pass
    module_dict = sys.modules["PySide6.QtGui"].__dict__
    # Enough built-in methods that the loop registers some that another thread frees
    names = [f"_churn{n}" for n in range(50000)]
    for name in names:
        module_dict[name] = [].append
    stop = threading.Event()

    def churn():
        i = 0
        while not stop.is_set():
            for name in names:
                del module_dict[name]
            for name in names:
                module_dict[name] = [].append
            i += 1

    churner = threading.Thread(target=churn)
    churner.start()
    if HAS_HOOKS:
        Shiboken.releaseFailpoint("module-exec-before-types")
    loader.join()
    stop.set()
    churner.join()
    for name in names:
        del module_dict[name]
    print(f"result {'ok' if not errors else errors}", flush=True)


def _import_waits():
    import importlib
    errors = []

    def load(name):
        try:
            importlib.import_module(name)
        except BaseException as e:      # noqa: B036
            errors.append(f"{name}: {e!r}")

    assert Shiboken.armFailpoint("module-exec-before-types")
    first = threading.Thread(target=load, args=("PySide6.QtGui",))
    first.start()
    assert wait_reached("module-exec-before-types")
    # QtWidgets needs QtGui, which is in sys.modules but has no types yet
    second = threading.Thread(target=load, args=("PySide6.QtWidgets",))
    second.start()
    second.join(0.5)                    # waits for QtGui's exec when imports wait
    Shiboken.releaseFailpoint("module-exec-before-types")
    first.join()
    second.join()
    from PySide6 import QtWidgets
    ok = not errors and hasattr(QtWidgets, "QWidget")
    print(f"result {'ok' if ok else errors}", flush=True)


def _generic_enums(threads=4, classes=120, enums=40):
    from enum import Enum
    from PySide6.QtCore import QEnum
    body = "".join(f"    @QEnum\n    class E{k}(Enum):\n        A = {k}\n" for k in range(enums))
    barrier = threading.Barrier(threads)
    errors = []

    def make(tag):
        namespace = {"QObject": QObject, "QEnum": QEnum, "Enum": Enum}
        barrier.wait()
        try:
            for c in range(classes):
                exec(f"class G{tag}_{c}(QObject):\n{body}", namespace)
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    makers = [threading.Thread(target=make, args=(t,)) for t in range(threads)]
    for t in makers:
        t.start()
    for t in makers:
        t.join()
    print(f"errors {len(errors)} {errors[:1]}", flush=True)


def made_value(name):
    tag, i = (int(x) for x in name.split("_"))
    return tag * 10000 + i


class SignalsOfNewInstanceTest(unittest.TestCase):
    '''A new QObject has the signals its class has now.'''

    def testSignalAddedToClass(self):
        class Sub(QObject):
            pass

        self.assertNotIn("late", vars(Sub()))
        Sub.late = Signal()
        self.assertIn("late", vars(Sub()), "new instance misses a signal of its class")
        del Sub.late
        self.assertNotIn("late", vars(Sub()), "new instance has a removed signal")

    def testSignalAddedToBase(self):
        class Base(QObject):
            pass

        class Derived(Base):
            pass

        self.assertNotIn("late", vars(Derived()))
        Base.late = Signal()
        self.assertIn("late", vars(Derived()), "new instance misses a signal of its base")

    def testSignalAddedToMixin(self):
        '''A base of another metatype: a write to it does not pass Shiboken.ObjectType.'''
        class Mixin:
            pass

        class Sub(Mixin, QObject):
            pass

        self.assertNotIn("late", vars(Sub()))
        Mixin.late = Signal()
        self.assertIn("late", vars(Sub()), "new instance misses a signal of its mixin")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testClassWrittenDuringListWalk(self):
        '''A write while the first list of a class is made: the next instance sees it.'''
        class Sub(QObject):
            pass

        self.assertTrue(Shiboken.armFailpoint("signal-list-after-walk"))
        maker = threading.Thread(target=Sub)
        maker.start()
        self.assertTrue(wait_reached("signal-list-after-walk"))
        Sub.late = Signal()
        Shiboken.releaseFailpoint("signal-list-after-walk")
        maker.join()
        self.assertIn("late", vars(Sub()), "new instance misses the signal written during the walk")

    def testBasesAssigned(self):
        class First(QObject):
            first = Signal()

        class Second(QObject):
            second = Signal()

        class Sub(First):
            pass

        self.assertIn("first", vars(Sub()))
        Sub.__bases__ = (Second,)
        names = vars(Sub())
        self.assertIn("second", names, "new instance misses a signal of its new base")
        self.assertNotIn("first", names, "new instance has a signal of its old base")

    def testClassChangedOftenerThanItHasVersions(self):
        '''The list does not depend on tp_version_tag: a class changed more often
        than CPython gives it tags (1000) still shows each signal.'''
        class Sub(QObject):
            pass

        for _ in range(1100):
            Sub.late = Signal()
            self.assertIn("late", vars(Sub()), "new instance misses a signal of its class")
            del Sub.late
            self.assertNotIn("late", vars(Sub()), "new instance has a removed signal")

    def exhaustVersions(self, cls):
        '''Uses up the version tags of a class: CPython gives it 1000 at most.'''
        assign = ctypes.pythonapi.PyUnstable_Type_AssignVersionTag
        assign.argtypes = [ctypes.py_object]
        assign.restype = ctypes.c_int
        for i in range(1100):
            cls.churn = i
            assign(cls)
        cls.churn = -1
        self.assertEqual(assign(cls), 0, "the class still gets versions")

    def testNoVersionAtFirstConstruction(self):
        '''With the tags used up before the first construction, a new instance
        still sees every signal.'''
        class Sub(QObject):
            pass

        self.exhaustVersions(Sub)
        Sub.late = Signal()
        self.assertIn("late", vars(Sub()), "new instance misses a signal of its class")
        del Sub.late
        self.assertNotIn("late", vars(Sub()), "new instance has a removed signal")

    def testVersionsRunOutAfterFirstConstruction(self):
        '''With the tags used up after the list was made, a new instance still
        sees a later signal.'''
        class Sub(QObject):
            pass

        self.assertNotIn("late", vars(Sub()))
        self.exhaustVersions(Sub)
        Sub.late = Signal()
        self.assertIn("late", vars(Sub()), "new instance misses a signal of its class")

    def testBaseCollectedWithClass(self):
        '''The list keeps no base alive: one gc pass collects a class and its base.'''
        def make():
            class Base(QObject):
                pass

            class Sub(Base):
                pass

            Sub()
            return weakref.ref(Base), weakref.ref(Sub)

        base, sub = make()
        gc.collect()
        self.assertIsNone(sub(), "the class outlives a gc pass")
        self.assertIsNone(base(), "the base outlives the gc pass that collects its class")

    def testLastWriteSeen(self):
        '''Writers and constructors meet on fresh classes: the last write counts.'''
        # With the GIL on, thread starts and writers wait for the makers. A short switch
        # interval, and no object dying in the race (a destructor gives the GIL away),
        # keep that to a fraction of a second.
        self.addCleanup(sys.setswitchinterval, sys.getswitchinterval())
        sys.setswitchinterval(1e-4)
        for _ in range(150):
            class Sub(QObject):
                pass

            Sub()
            done = threading.Event()
            made = []

            def write(k):
                for j in range(8):
                    setattr(Sub, f"w{k}_{j}", Signal())
                setattr(Sub, f"last{k}", Signal())

            def construct():
                while not done.is_set():
                    made.append(Sub())

            writers = [threading.Thread(target=write, args=(k,)) for k in range(2)]
            makers = [threading.Thread(target=construct) for _ in range(2)]
            for t in makers + writers:
                t.start()
            for t in writers:
                t.join()
            done.set()
            for t in makers:
                t.join()
            made.clear()
            names = vars(Sub())
            for k in range(2):
                self.assertIn(f"last{k}", names, "new instance misses the last written signal")

    def testConstructionWaitsForClassWrite(self):
        '''A construction while a class write is under way sees the written signal.

        A dict watcher runs after CPython cleared the class version and before
        it stores: its Python code keeps the locks of the write unless it
        blocks. With SIGNAL_LIST_DETACH_IN_WRITE it blocks, which frees them
        for the other thread; without the write count (ClassWriteCount) that
        thread takes the stale list then: the proof that this test sees it.
        '''
        class Sub(QObject):
            pass

        detach = "SIGNAL_LIST_DETACH_IN_WRITE" in os.environ
        Sub()
        typeDict = next(r for r in gc.get_referents(Sub.__dict__) if isinstance(r, dict))
        inWrite = [0]

        def onWrite(event, _dict, _key, _value):
            if event == 0:  # PyDict_EVENT_ADDED
                inWrite[0] = 1
                if detach:
                    time.sleep(0.05)
                else:
                    for _ in range(200000):
                        pass
            return 0

        Callback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
                                    ctypes.c_void_p, ctypes.c_void_p)
        callback = Callback(onWrite)
        api = ctypes.pythonapi
        api.PyDict_AddWatcher.argtypes = [Callback]
        api.PyDict_AddWatcher.restype = ctypes.c_int
        api.PyDict_Watch.argtypes = [ctypes.c_int, ctypes.py_object]
        api.PyDict_Unwatch.argtypes = [ctypes.c_int, ctypes.py_object]
        api.PyDict_ClearWatcher.argtypes = [ctypes.c_int]

        def construct():
            while not inWrite[0]:
                pass
            Sub()

        watcher = api.PyDict_AddWatcher(callback)
        self.assertGreaterEqual(watcher, 0)
        wasEnabled = gc.isenabled()
        gc.disable()  # a stopped world would free the locks as well
        try:
            self.assertEqual(api.PyDict_Watch(watcher, typeDict), 0)
            maker = threading.Thread(target=construct)
            writer = threading.Thread(target=setattr, args=(Sub, "late", Signal()))
            maker.start()
            writer.start()
            writer.join()
            maker.join()
            api.PyDict_Unwatch(watcher, typeDict)
        finally:
            api.PyDict_ClearWatcher(watcher)
            if wasEnabled:
                gc.enable()
        self.assertIn("late", vars(Sub()), "new instance misses the written signal")

    def testClassChangedWhileConstructing(self):
        '''Other threads construct objects while one changes the class.'''
        class Sub(QObject):
            fixed = Signal()

        self.assertIn("fixed", vars(Sub()))  # The class has its list from here on
        stop = threading.Event()
        errors = []

        def change():
            while not stop.is_set():
                Sub.changing = Signal()
                del Sub.changing

        def construct():
            try:
                for _ in range(3000):
                    self.assertIn("fixed", vars(Sub()))
            except BaseException as e:
                errors.append(repr(e))

        changer = threading.Thread(target=change)
        makers = [threading.Thread(target=construct) for _ in range(4)]
        changer.start()
        for t in makers:
            t.start()
        for t in makers:
            t.join()
        stop.set()
        changer.join()
        self.assertEqual(errors, [])
        Sub.final = Signal()
        self.assertIn("final", vars(Sub()), "new instance misses a signal of its class")

    def testSignalsReleasedWithTheClass(self):
        '''A dead class holds on to none of its signals.'''
        signal = Signal()

        def make():
            class Sub(QObject):
                pass

            Sub.late = signal
            Sub()

        def settled():
            for _ in range(3):
                gc.collect()
            return sys.getrefcount(signal)

        make()  # what a first use leaves behind
        before = settled()
        for _ in range(50):
            make()
        self.assertLess(settled() - before, 25, "dead classes hold their signals")


if __name__ == '__main__':
    if "--qenums" in sys.argv:
        _qenums()
        sys.exit(0)
    if "--execs" in sys.argv:
        _execs()
        sys.exit(0)
    if "--conversions" in sys.argv:
        _conversions()
        sys.exit(0)
    if "--converters" in sys.argv:
        _converters()
        sys.exit(0)
    if "--post" in sys.argv:
        _post_routines()
        sys.exit(0)
    if "--module-dir" in sys.argv:
        _module_dir()
        sys.exit(0)
    if "--module-dict" in sys.argv:
        _module_dict()
        sys.exit(0)
    if "--import-waits" in sys.argv:
        _import_waits()
        sys.exit(0)
    if "--generic-enums" in sys.argv:
        _generic_enums()
        sys.exit(0)
    if "--stream-helper" in sys.argv:
        _stream_helper()
        sys.exit(0)
    unittest.main()
