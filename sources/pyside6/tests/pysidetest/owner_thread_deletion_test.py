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

from PySide6.QtCore import (QCoreApplication, QEvent, QObject, QRunnable,  # noqa: E402
                            QThread, Qt)
from shiboken6 import Shiboken  # noqa: E402

MAIN = threading.get_ident()
WRONG_THREAD = "not destroyed in the owner thread"
NEVER_DESTROYED = "never destroyed after its owner thread finished"
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")


def child_tail(output, lines=20):
    '''The last lines of a child process, for a failure message.'''
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    return "\n".join((output or "").splitlines()[-lines:])


def watch(obj, record):
    # A direct connection runs in the thread that runs ~QObject
    obj.destroyed.connect(lambda: record.append(threading.get_ident()),
                          Qt.ConnectionType.DirectConnection)


def settle(record, count=1):
    """Run deferred deletions until `count` arrived."""
    for _ in range(200):
        QCoreApplication.processEvents()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        if len(record) >= count:
            return
        time.sleep(0.01)


class Loop(QThread):
    def run(self):
        self.ident = threading.get_ident()
        self.exec()


class Finisher(QThread):
    """No event loop; run() returns when told."""

    def __init__(self):
        super().__init__()
        self.go = threading.Event()

    def run(self):
        self.ident = threading.get_ident()
        self.go.wait()


class EventObject(QObject):
    """The deferred deletion passes through a Python event() override."""

    def event(self, e):
        return super().event(e)


class Task(QObject, QRunnable):
    """Two wrapped bases: the wrapper owns two C++ objects."""

    def run(self):
        pass


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class OwnerThreadDeletionTest(unittest.TestCase):
    """A Python-owned QObject is destroyed in its own thread, wherever its wrapper dies."""

    @classmethod
    def setUpClass(cls):
        cls.app = QCoreApplication.instance() or QCoreApplication([])

    def testLastReferenceOnWorker(self):
        record = []
        holder = [QObject()]
        watch(holder[0], record)
        t = threading.Thread(target=holder.clear)
        t.start()
        t.join()
        settle(record)
        self.assertEqual(record, [MAIN], WRONG_THREAD)

    def testGcOnWorker(self):
        record = []
        gc.disable()
        try:
            obj = EventObject()
            child = QObject(obj)
            watch(obj, record)
            watch(child, record)
            obj.cycle = [obj]
            del obj, child
            t = threading.Thread(target=gc.collect)
            t.start()
            t.join()
        finally:
            gc.enable()
        settle(record, 2)
        self.assertEqual(record, [MAIN, MAIN], WRONG_THREAD)

    def testMultipleBases(self):
        record = []
        gc.disable()
        try:
            obj = Task()
            watch(obj, record)
            obj.cycle = [obj]
            del obj
            t = threading.Thread(target=gc.collect)
            t.start()
            t.join()
        finally:
            gc.enable()
        settle(record)
        self.assertEqual(record, [MAIN], WRONG_THREAD)

    def testMergedRefcount(self):
        record = []
        obj = QObject()
        watch(obj, record)
        holder = [obj, obj]
        del obj
        first = threading.Thread(target=holder.pop)
        first.start()
        first.join()
        for _ in range(20):          # main merges the shared refcount meanwhile
            time.sleep(0.01)
        t = threading.Thread(target=holder.clear)
        t.start()
        t.join()
        settle(record)
        self.assertEqual(record, [MAIN], WRONG_THREAD)

    def testMovedObject(self):
        record = []
        th = Loop()
        obj = QObject()
        watch(obj, record)
        obj.moveToThread(th)
        th.start()
        while not hasattr(th, "ident"):
            time.sleep(0.01)
        del obj
        for _ in range(200):
            if record:
                break
            time.sleep(0.01)
        th.quit()
        th.wait()
        self.assertEqual(record, [th.ident], WRONG_THREAD)

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testOwnerFinishesBeforePost(self):
        """The owner runs at the check and finishes before the deleteLater() is posted."""
        record = []
        th = Finisher()
        th.start()

        def drop():
            # Created here: a wrapper of another thread would die in that thread
            obj = QObject()
            watch(obj, record)
            obj.moveToThread(th)
            del obj

        point = "delete-later-before-post"
        self.assertTrue(Shiboken.armFailpoint(point))
        try:
            t = threading.Thread(target=drop)
            t.start()
            # Without OwnerThreadDeletion, t deletes the object at once and never parks
            while t.is_alive() and not Shiboken.failpointReached(point):
                time.sleep(0.01)
            th.go.set()
            th.wait()               # its last flush of deferred deletions is over
        finally:
            Shiboken.releaseFailpoint(point)
            t.join()
        settle(record)
        self.assertEqual(record, [t.ident], NEVER_DESTROYED)

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFinishedSlotWaitsForDropper(self):
        """A finished() slot of the owner waits for a lock the dropping thread holds."""
        try:
            proc = subprocess.run([sys.executable, __file__, "--finished-slot"],
                                  capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as e:
            self.fail(f"the child hangs\n{child_tail(e.stderr)}")
        self.assertIn("dropped, destroyed in the owner", proc.stdout,
                      f"rc {proc.returncode}: {proc.stdout}\n{child_tail(proc.stderr)}")

    def testExitTeardown(self):
        proc = subprocess.run([sys.executable, __file__, "--exit"],
                              capture_output=True, text=True, timeout=120)
        out = proc.stdout + proc.stderr
        self.assertIn("exiting", out)
        self.assertNotIn("destroyed", out, "deleted another running thread's object at exit")


def _exit_with_running_thread():
    """A daemon thread's object is still referenced at exit."""
    app = QCoreApplication([])  # noqa: F841
    started = threading.Event()
    keep = []

    def worker():
        obj = QObject()
        obj.destroyed.connect(lambda: os.write(1, b"destroyed\n"),
                              Qt.ConnectionType.DirectConnection)
        keep.append(obj)
        started.set()
        time.sleep(600)

    threading.Thread(target=worker, daemon=True).start()
    started.wait()
    print("exiting", flush=True)


def _finished_slot_waits_for_dropper():
    """The dropping thread holds a lock and parks between the owner check and the
    post; the owner finishes, and its finished() slot waits for that lock before
    the owner flushes its deferred deletions. The dropping thread must not wait."""
    app = QCoreApplication([])  # noqa: F841
    lock = threading.Lock()
    in_slot = threading.Event()
    record = []
    th = Finisher()

    def on_finished():
        in_slot.set()
        with lock:
            pass

    th.finished.connect(on_finished, Qt.ConnectionType.DirectConnection)
    th.start()

    def drop():
        obj = QObject()
        watch(obj, record)
        obj.moveToThread(th)
        with lock:
            del obj

    point = "delete-later-before-post"
    Shiboken.armFailpoint(point, 20000)
    t = threading.Thread(target=drop, daemon=True)
    t.start()
    while t.is_alive() and not Shiboken.failpointReached(point):
        time.sleep(0.01)
    th.go.set()
    in_slot.wait(5)
    Shiboken.releaseFailpoint(point)
    t.join(10)
    if t.is_alive():
        print("the dropping thread hangs", flush=True)
        os._exit(3)
    th.wait()
    settle(record)
    where = "in the owner" if record == [th.ident] else f"at {record}"
    print(f"dropped, destroyed {where}", flush=True)


if __name__ == '__main__':
    if "--finished-slot" in sys.argv:
        _finished_slot_waits_for_dropper()
        sys.exit(0)
    if "--exit" in sys.argv:
        _exit_with_running_thread()
        sys.exit(0)
    unittest.main()
