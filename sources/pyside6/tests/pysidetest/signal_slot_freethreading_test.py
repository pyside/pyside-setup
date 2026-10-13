# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Signals, slots and dynamic meta objects, against a second thread.'''

import gc
import os
import subprocess
import sys
import sysconfig
import threading
import time
import unittest
import warnings
import weakref

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import ClassInfo, QObject, QThread, Signal, SIGNAL  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")
BUILDER_BROKEN = "a dynamic method was lost"
HASH_BROKEN = "the connection hash broke"


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


def run_stress(test, option, broken):
    '''A stress in a fresh process: a broken state usually ends it.'''
    for _ in range(3):
        try:
            proc = subprocess.run([sys.executable, __file__, option],
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            test.fail(f"{broken}: the run hangs")
        test.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        test.assertIn("wrong 0", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")


class NoWeakReceiver:
    '''A receiver that takes no weak references.'''
    __slots__ = ("tag",)

    def __init__(self, tag):
        self.tag = tag

    def handler(self):
        delivered.append(self.tag)


class CompiledMethod:
    '''Looks like a compiled (Nuitka) method to libpyside.'''

    def __init__(self, func, obj):
        self.im_func = func
        self.im_self = obj
        self.__code__ = func.__code__
        self.__name__ = func.__name__

    def __call__(self, *args):
        return self.im_func(self.im_self, *args)


delivered = []


@unittest.skipUnless(FREE_THREADED, "free-threaded builds only")
class SignalSlotFreeThreadedTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()
        delivered.clear()

    def testDynamicMethodsFromTwoThreads(self):
        '''Dynamic signals added from two threads, read from a third: none is lost.'''
        run_stress(self, "--builder", BUILDER_BROKEN)

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstDynamicMethodFromTwoThreads(self):
        '''First dynamic signals of an object in two threads share one meta object.'''
        sender = QObject()
        base_methods = sender.metaObject().methodCount()

        def connect(name):
            QObject.connect(sender, SIGNAL(f"{name}()"), lambda: None)

        self.assertTrue(Shiboken.armFailpoint("metaobject-before-commit"))
        parked = threading.Thread(target=connect, args=("alpha",))
        parked.start()
        self.assertTrue(wait_reached("metaobject-before-commit"))
        connect("beta")
        Shiboken.releaseFailpoint("metaobject-before-commit")
        parked.join()
        meta = sender.metaObject()
        found = [meta.indexOfSignal(f"{name}()") >= 0 for name in ("alpha", "beta")]
        self.assertEqual((found, meta.methodCount() - base_methods), ([True, True], 2),
                         "the object got two meta-object builders")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testSameDynamicMethodFromTwoThreads(self):
        '''One dynamic signal added from two threads exists once.'''
        sender = QObject()
        QObject.connect(sender, SIGNAL("first()"), lambda: None)    # the builder exists
        base_methods = sender.metaObject().methodCount()

        def connect():
            QObject.connect(sender, SIGNAL("same()"), lambda: None)

        self.assertTrue(Shiboken.armFailpoint("method-before-add"))
        parked = threading.Thread(target=connect)
        parked.start()
        self.assertTrue(wait_reached("method-before-add"))
        connect()
        Shiboken.releaseFailpoint("method-before-add")
        parked.join()
        added = sender.metaObject().methodCount() - base_methods
        self.assertEqual((added, sender.receivers(SIGNAL("same()"))), (1, 2),
                         "one dynamic signal was added twice")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testDecoratedClassFromTwoThreads(self):
        '''A subclass made during the first read shares the meta object.'''
        @ClassInfo(author="ft")
        class Decorated(QObject):
            pass

        read, made = [], []
        self.assertTrue(Shiboken.armFailpoint("builder-before-build"))
        reader = threading.Thread(target=lambda: read.append(Decorated().metaObject()))
        reader.start()
        self.assertTrue(wait_reached("builder-before-build"))
        # type(): the class statement is not what is tested here.
        maker = threading.Thread(target=lambda: made.append(type("Sub", (Decorated,), {})))
        maker.start()
        maker.join(0.5)     # with the lock it waits for the reader
        Shiboken.releaseFailpoint("builder-before-build")
        reader.join()
        maker.join()
        base = made[0].staticMetaObject.superClass()
        self.assertEqual(Shiboken.getCppPointer(base), Shiboken.getCppPointer(read[0]),
                         "one class got two meta objects")

    def testConnectionsFromThreads(self):
        '''Connect and disconnect from threads keeps the hash intact.'''
        run_stress(self, "--hash", HASH_BROKEN)

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testReceiverDropsMidDelivery(self):
        '''A delivery binds only a receiver it holds.'''
        class Sender(QObject):
            fired = Signal()

        class Receiver:
            def handler(self):
                delivered.append(self.tag)

        receiver = Receiver()
        receiver.tag = "receiver"
        alive = weakref.ref(receiver)
        handler = receiver.handler

        def emit():
            # Sender and delivery in this thread: a direct connection
            sender = Sender()
            sender.fired.connect(handler)
            sender.fired.emit()

        self.assertTrue(Shiboken.armFailpoint("method-receiver-before-bind"))
        emitter = threading.Thread(target=emit)
        emitter.start()
        self.assertTrue(wait_reached("method-receiver-before-bind"))
        receiver = handler = None
        gc.collect()
        held = alive() is not None
        ballast = [Receiver() for _ in range(2000)]  # noqa: F841
        Shiboken.releaseFailpoint("method-receiver-before-bind")
        emitter.join()
        self.assertEqual((held, delivered), (True, ["receiver"]),
                         "the delivery bound a receiver it did not hold")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testSourceDroppedDuringEmit(self):
        '''A source dropped while another thread emits its signal lasts the emit.'''
        class Sender(QObject):
            fired = Signal()

        sender = Sender()
        fired = sender.fired
        fired.emit()                # the signal index is known from here on
        alive = weakref.ref(sender)
        results = []

        def emit():
            try:
                results.append(fired.emit())
            except Exception as e:      # noqa: BLE001
                results.append(repr(e))

        self.assertTrue(Shiboken.armFailpoint("signal-emit-after-check"))
        emitter = threading.Thread(target=emit)
        emitter.start()
        self.assertTrue(wait_reached("signal-emit-after-check"))
        sender = None
        gc.collect()
        held = alive() is not None
        Shiboken.releaseFailpoint("signal-emit-after-check")
        emitter.join()
        self.assertEqual((held, results), (True, [True]), "a signal used a dropped source")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstSignalAccessFromTwoThreads(self):
        '''First binding of a signal in two threads gives both the same instance.'''
        sender = QThread.currentThread()
        first = []
        self.assertTrue(Shiboken.armFailpoint("signal-instance-before-store"))
        parked = threading.Thread(target=lambda: first.append(sender.objectNameChanged))
        parked.start()
        self.assertTrue(wait_reached("signal-instance-before-store"))
        second = sender.objectNameChanged
        Shiboken.releaseFailpoint("signal-instance-before-store")
        parked.join()
        self.assertTrue(first[0] is second and sender.objectNameChanged is second,
                        "two SignalInstances for one signal")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstConnectsFromTwoThreads(self):
        '''The process's first connections in two threads make one tracker.'''
        proc = subprocess.run([sys.executable, __file__, "--tracker"],
                              capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0,
                         f"two sender trackers: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("trackers 1", proc.stdout,
                      f"two sender trackers\n{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstUseDuringStopTheWorld(self):
        '''A thread waiting for a first use does not hold up a stop-the-world.'''
        try:
            proc = subprocess.run([sys.executable, __file__, "--first-use-stop"],
                                  capture_output=True, text=True, timeout=15)
        except subprocess.TimeoutExpired:
            self.fail("a stop-the-world waited for a first use: the run hangs")
        self.assertEqual(proc.returncode, 0,
                         f"a first use: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("wrong 0", proc.stdout, f"a first use\n{child_tail(proc.stderr)}")

    def testReceiverWithoutWeakref(self):
        '''A receiver without weak references stays alive while connected.'''
        class Sender(QObject):
            fired = Signal()

        sender = Sender()
        receiver = NoWeakReceiver("receiver")
        sender.fired.connect(receiver.handler)
        del receiver
        gc.collect()
        ballast = [NoWeakReceiver("ballast") for _ in range(2000)]  # noqa: F841
        sender.fired.emit()
        self.assertEqual(delivered, ["receiver"], "the connection outlived its receiver")

    def testRepeatedConnectionsGoWithReceiver(self):
        '''Both connections of a method go with its receiver.'''
        class Sender(QObject):
            fired = Signal()

        class Receiver:
            def handler(self):
                delivered.append(1)

        sender = Sender()
        receiver = Receiver()
        sender.fired.connect(receiver.handler)
        sender.fired.connect(receiver.handler)
        sender.fired.emit()
        del receiver
        gc.collect()
        self.assertEqual((len(delivered), sender.receivers(SIGNAL("fired()"))), (2, 0),
                         "a repeated connection outlived its receiver")

    def testRepeatedConnectionsDisconnect(self):
        '''A method connected twice is disconnected twice.'''
        class Sender(QObject):
            fired = Signal()

        class Receiver:
            def handler(self):
                delivered.append(1)

        sender = Sender()
        receiver = Receiver()
        sender.fired.connect(receiver.handler)
        sender.fired.connect(receiver.handler)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", RuntimeWarning)
            done = [sender.fired.disconnect(receiver.handler) for _ in range(2)]
        sender.fired.emit()
        self.assertEqual((done, delivered), ([True, True], []),
                         "a repeated connection could not be disconnected")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testMainQueuedSourceCountsAsDeleted(self):
        '''A widget queued for deletion in the main thread counts as deleted.'''
        broken = "a signal used a source queued for the main thread"
        proc = subprocess.run([sys.executable, __file__, "--main-queued-source"],
                              capture_output=True, text=True, timeout=60,
                              env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("result ['RuntimeError']", proc.stdout,
                      f"{broken}\n{proc.stdout}{child_tail(proc.stderr)}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testSourceDroppedDuringOperation(self):
        '''A source dropped during a connect or disconnect in another thread lasts it.'''
        broken = "a signal operation used a dropped source"
        for op in ("connect-callable", "disconnect-all", "disconnect-callable"):
            with self.subTest(op=op):
                proc = subprocess.run([sys.executable, __file__, "--source-op", op],
                                      capture_output=True, text=True, timeout=60)
                self.assertEqual(proc.returncode, 0, f"{broken} ({op}): rc {proc.returncode}"
                                 f"\n{child_tail(proc.stderr)}")
                self.assertIn("result held=True [True]", proc.stdout,
                              f"{broken} ({op})\n{proc.stdout}{child_tail(proc.stderr)}")

    def testSourceDroppedInAnotherThread(self):
        '''A source dropped in another thread counts as deleted, as with a GIL.'''
        proc = subprocess.run([sys.executable, __file__, "--dropped-source"],
                              capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0,
                         f"a signal used a dropped source: rc {proc.returncode}\n"
                         f"{child_tail(proc.stderr)}")
        self.assertIn("wrong 0", proc.stdout,
                      f"a signal used a dropped source\n{proc.stdout}{child_tail(proc.stderr)}")

    def testSignatureWarningHookWaits(self):
        '''A signature warning hook waiting for a thread does not hang.'''
        class Obj(QObject):
            def handler(self):
                pass

        def other():
            obj = Obj()
            QObject.connect(obj, SIGNAL("other()"), obj.handler)

        threads = []
        stuck = []

        def hook(*args, **kwargs):
            thread = threading.Thread(target=other)
            threads.append(thread)
            thread.start()
            thread.join(5)
            stuck.append(thread.is_alive())

        with warnings.catch_warnings():
            warnings.simplefilter("always")
            warnings.showwarning = hook
            obj = Obj()
            QObject.connect(obj, SIGNAL("unbracketed"), obj.handler)
        for thread in threads:
            thread.join()
        self.assertEqual(stuck, [False], "a warning hook waited for the builder lock")

    def testLookupKeyWaits(self):
        '''A dict key's __eq__ waiting for a thread does not hang.'''
        try:
            proc = subprocess.run([sys.executable, __file__, "--lookup-key"],
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            self.fail("a dictionary key waited for the builder lock: the run hangs")
        self.assertEqual(proc.returncode, 0,
                         f"a dictionary key waited for the builder lock: rc {proc.returncode}\n"
                         f"{child_tail(proc.stderr)}")
        self.assertIn("wrong 0", proc.stdout,
                      f"a dictionary key waited for the builder lock\n{proc.stdout}"
                      f"{child_tail(proc.stderr)}")

    def testCompiledMethodFunction(self):
        '''A compiled method's function stays referenced.'''
        class Sender(QObject):
            fired = Signal()

        class Receiver:
            def handler(self):
                pass

        sender = Sender()
        receiver = Receiver()
        method = CompiledMethod(Receiver.handler, receiver)
        func = Receiver.handler
        before = sys.getrefcount(func)
        for _ in range(5):
            sender.fired.connect(method)
            sender.fired.disconnect(method)
        self.assertEqual(sys.getrefcount(func), before,
                         "the slot released a reference it never owned")


def _builder(names=300):
    sender = QObject()
    QObject.connect(sender, SIGNAL("first()"), lambda: None)    # the builder exists
    base = sender.metaObject().methodCount()
    stop = threading.Event()
    barrier = threading.Barrier(3)

    def add(tag):
        barrier.wait()
        for i in range(names):
            QObject.connect(sender, SIGNAL(f"{tag}{i}()"), lambda: None)

    def read():
        barrier.wait()
        while not stop.is_set():
            sender.metaObject().methodCount()

    adders = [threading.Thread(target=add, args=(tag,)) for tag in "ab"]
    reader = threading.Thread(target=read)
    for t in adders + [reader]:
        t.start()
    for t in adders:
        t.join()
    stop.set()
    reader.join()
    meta = sender.metaObject()
    missing = sum(meta.indexOfSignal(f"{tag}{i}()") < 0 for tag in "ab" for i in range(names))
    wrong = missing + abs(meta.methodCount() - base - 2 * names)
    print(f"wrong {wrong}", flush=True)


def _source_op(op):
    class Sender(QObject):
        fired = Signal()

    def handler():
        pass

    sender = Sender()
    fired = sender.fired
    alive = weakref.ref(sender)
    if op != "connect-callable":
        fired.connect(handler)
    results = []

    def work():
        try:
            if op == "connect-callable":
                results.append(bool(fired.connect(handler)))
            elif op == "disconnect-all":
                results.append(fired.disconnect())
            else:
                results.append(fired.disconnect(handler))
        except Exception as e:      # noqa: BLE001
            results.append(repr(e))

    assert Shiboken.armFailpoint("signal-op-after-check")
    worker = threading.Thread(target=work)
    worker.start()
    assert wait_reached("signal-op-after-check")
    sender = None
    gc.collect()
    held = alive() is not None
    Shiboken.releaseFailpoint("signal-op-after-check")
    worker.join()
    print(f"result held={held} {results}", flush=True)


def _main_queued_source():
    from PySide6.QtWidgets import QApplication, QWidget
    app = QApplication([])  # noqa: F841

    class Widget(QWidget):
        fired = Signal()

    box = [Widget()]
    fired = box[0].fired
    fired.emit()                # the signal index is known from here on
    mine = []
    result = []
    taken = threading.Event()
    go = threading.Event()

    def worker():
        mine.append(box[0])     # a reference counted in this thread
        taken.set()
        go.wait()
        # The main thread holds none: the wrapper goes in this thread, and its
        # widget is queued for deletion in the main thread
        mine.clear()
        try:
            result.append(fired.emit())
        except RuntimeError:
            result.append("RuntimeError")

    def watch():
        wait_reached("signal-emit-after-check", 2.0)

    assert Shiboken.armFailpoint("signal-emit-after-check")
    emitter = threading.Thread(target=worker)
    emitter.start()
    taken.wait()
    box.clear()
    watcher = threading.Thread(target=watch)
    go.set()
    watcher.start()
    # Blocked in C, so the queued deletion cannot run before the worker's check
    watcher.join()
    if Shiboken.failpointReached("signal-emit-after-check"):
        # The source passed the check: the main thread deletes it meanwhile
        for _ in range(200):
            time.sleep(0.001)
        Shiboken.releaseFailpoint("signal-emit-after-check")
    emitter.join()
    print(f"result {result}", flush=True)


def _hash(threads=8, rounds=300):
    class Sender(QObject):
        fired = Signal()

    class Receiver:
        def handler(self):
            delivered.append(1)

    sender = Sender()
    barrier = threading.Barrier(threads)
    errors = []
    done = []

    def churn():
        try:
            receivers = [Receiver() for _ in range(10)]
            barrier.wait()
            for i in range(rounds):
                receiver = receivers[i % len(receivers)]
                sender.fired.connect(receiver.handler)
                sender.fired.disconnect(receiver.handler)
            done.append(rounds)
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    workers = [threading.Thread(target=churn) for _ in range(threads)]
    for t in workers:
        t.start()
    for t in workers:
        t.join()
    sender.fired.emit()
    # A worker that failed made no connections: no delivery would be no evidence
    wrong = len(delivered) + len(errors) + abs(sum(done) - threads * rounds)
    print(f"wrong {wrong} {errors[:1]}", flush=True)


def _tracker():
    from PySide6.QtCore import QCoreApplication, QEvent
    app = QCoreApplication([])

    class Sender(QObject):
        fired = Signal()

    first, second = Sender(), Sender()

    def handler():
        pass

    assert Shiboken.armFailpoint("sender-tracker-before-create")
    worker = threading.Thread(target=lambda: first.fired.connect(handler))
    worker.start()
    assert wait_reached("sender-tracker-before-create")
    second.fired.connect(handler)       # makes and publishes the tracker
    Shiboken.releaseFailpoint("sender-tracker-before-create")
    worker.join()
    for _ in range(3):                  # the worker's queued reparent or deleteLater
        QCoreApplication.processEvents()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    trackers = [c for c in app.children()
                if c.metaObject().className().endswith("SenderSignalDeletionTracker")]
    print(f"trackers {len(trackers)}", flush=True)


class _Source(QObject):
    fired = Signal()


def _dropped_source():
    from PySide6.QtCore import QCoreApplication
    app = QCoreApplication([])  # noqa: F841

    def local_source():
        class Local(QObject):
            fired = Signal()
        return Local()

    wrong = 0
    for make in (_Source, local_source):
        for use in ("call", "connect", "emit"):
            # The wrapper must die in the dropping thread with its C++ object
            # alive; a round where main merges the count first repeats
            for _ in range(100):
                holder = [make()]
                signal = holder[0].fired
                died = []
                destroyed = []
                ref = weakref.ref(holder[0],
                                  lambda _ref: died.append(threading.current_thread()))
                holder[0].destroyed.connect(lambda: destroyed.append(True))

                def drop():
                    holder.clear()
                    gc.collect()

                thread = threading.Thread(target=drop)
                thread.start()
                thread.join()
                if died == [thread] and not destroyed:
                    break
            else:
                wrong += 1
                print(f"{make.__name__} {use}: the source never died in the other thread",
                      flush=True)
                continue
            del ref
            try:
                if use == "call":
                    signal()
                elif use == "connect":
                    signal.connect(lambda: None)
                else:
                    signal.emit()
                outcome = "no error"
            except RuntimeError as e:
                outcome = "deleted" if "deleted" in str(e) else f"RuntimeError {e}"
            except Exception as e:
                outcome = f"{type(e).__name__} {e}"
            if outcome != "deleted":
                wrong += 1
                print(f"{make.__name__} {use}: {outcome}", flush=True)
    print(f"wrong {wrong}", flush=True)


def _first_use_stop():
    class Sender(QObject):
        fired = Signal()

    class Receiver:
        def handler(self):
            pass

    sender, receiver = Sender(), Receiver()
    # The first connect of a bound method reaches function-local statics
    assert Shiboken.armFailpoint("once-value-create", 3000)
    first = threading.Thread(target=lambda: sender.fired.connect(receiver.handler), daemon=True)
    first.start()
    assert wait_reached("once-value-create")
    second = threading.Thread(target=lambda: sender.fired.connect(receiver.handler), daemon=True)
    second.start()
    time.sleep(0.5)     # the second thread waits for the value of the first
    gc.collect()        # stops the world
    Shiboken.releaseFailpoint("once-value-create")
    for t in (first, second):
        t.join(10)
    print(f"wrong {int(first.is_alive() or second.is_alive())}", flush=True)
    os._exit(0)     # a hung thread would hold up the exit


def _lookup_key():
    lock = threading.Lock()
    holding = threading.Event()
    comparing = threading.Event()

    class Collider:
        '''A key with the hash of the instance builder name.'''

        def __hash__(self):
            return hash("__METAOBJECT__")

        def __eq__(self, other):
            comparing.set()
            with lock:
                pass
            return False

    class Obj(QObject):
        pass

    obj, other = Obj(), Obj()
    obj.__dict__[Collider()] = None

    def look_up():
        holding.wait()
        obj.metaObject()

    def hold():
        with lock:
            holding.set()
            comparing.wait(5)
            other.metaObject()

    threads = [threading.Thread(target=f, daemon=True) for f in (look_up, hold)]
    for t in threads:
        t.start()
    deadline = time.monotonic() + 10
    for t in threads:
        t.join(max(0.0, deadline - time.monotonic()))
    hung = any(t.is_alive() for t in threads)
    if hung:
        print("the lookup and the lock holder wait for each other", flush=True)
    if not comparing.is_set():
        print("the key's __eq__ did not run", flush=True)
    print(f"wrong {int(hung or not comparing.is_set())}", flush=True)
    os._exit(0)     # a hung thread would hold up the exit


if __name__ == '__main__':
    if "--source-op" in sys.argv:
        _source_op(sys.argv[sys.argv.index("--source-op") + 1])
        sys.exit(0)
    if "--main-queued-source" in sys.argv:
        _main_queued_source()
        sys.exit(0)
    if "--lookup-key" in sys.argv:
        _lookup_key()
    if "--first-use-stop" in sys.argv:
        _first_use_stop()
    if "--dropped-source" in sys.argv:
        _dropped_source()
        sys.exit(0)
    if "--tracker" in sys.argv:
        _tracker()
        sys.exit(0)
    if "--builder" in sys.argv:
        _builder()
        sys.exit(0)
    if "--hash" in sys.argv:
        _hash()
        sys.exit(0)
    unittest.main()
