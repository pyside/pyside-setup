# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Dynamic QtRemoteObjects types, against a second thread.'''

import gc
import os
import subprocess
import sys
import sysconfig
import threading
import unittest
import weakref

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

try:
    from PySide6.QtRemoteObjects import RepFile
except ImportError:
    RepFile = None

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
BROKEN = "a source property was read while another thread set it"

CONTENTS = """
class Holder
{
    PROP(QString s READWRITE);
    SLOT(void pushS(QString s));
};
"""


def child_tail(output, lines=20):
    '''The last lines of a child process, for a failure message.'''
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    return "\n".join((output or "").splitlines()[-lines:])


@unittest.skipUnless(FREE_THREADED, "free-threaded builds only")
@unittest.skipUnless(RepFile, "needs QtRemoteObjects")
class RemoteObjectsFreeThreadedTest(unittest.TestCase):

    def testBoundMethodHoldsInstance(self):
        '''A bound method of a dynamic source outlives the other references.'''
        source = RepFile(CONTENTS).source["Holder"]()
        alive = weakref.ref(source)
        method = source.pushS
        del source
        gc.collect()
        self.assertIsNotNone(alive(), "a bound capsule method did not hold its instance")
        method("pushed")
        self.assertEqual(alive().s, "pushed")

    def testPropertyReadWhileSet(self):
        '''A source property read while another thread sets it is intact.'''
        for _ in range(8):      # one run may pass by luck
            try:
                proc = subprocess.run([sys.executable, __file__, "--property"],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired:
                self.fail(f"{BROKEN}: the run hangs")
            self.assertEqual(proc.returncode, 0,
                             f"{BROKEN}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("wrong 0", proc.stdout, f"{BROKEN}\n{child_tail(proc.stderr)}")

    def testTypesFromThreads(self):
        '''Dynamic types created in several threads all succeed.'''
        broken = "a type creation failed"
        for _ in range(6):      # one run may pass by luck
            try:
                proc = subprocess.run([sys.executable, __file__, "--types"],
                                      capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired as e:
                self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
            self.assertEqual(proc.returncode, 0,
                             f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
            self.assertIn("errors 0", proc.stdout, f"{broken}\n{child_tail(proc.stderr)}")

    def testTypeCreationWithoutStopTheWorld(self):
        '''Making a dynamic type does not wait for a thread busy in C code.'''
        broken = "a type creation waited for a stop-the-world"
        try:
            proc = subprocess.run([sys.executable, __file__, "--stw"],
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired as e:
            self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("waited 0", proc.stdout, f"{broken}\n{child_tail(proc.stdout)}")

    def testWarningDropsType(self):
        '''A message handler run by a registry warning can drop a dynamic type.'''
        broken = "the type registry lock was taken twice by one thread"
        try:
            proc = subprocess.run([sys.executable, __file__, "--warning"],
                                  capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired as e:
            self.fail(f"{broken}: the run hangs\n{child_tail(e.stderr)}")
        self.assertEqual(proc.returncode, 0,
                         f"{broken}: rc {proc.returncode}\n{child_tail(proc.stderr)}")
        self.assertIn("dropped 1", proc.stdout,
                      f"the handler dropped no type\n{child_tail(proc.stdout)}")


def _warning():
    from PySide6.QtCore import qInstallMessageHandler
    from PySide6.QtRemoteObjects import getCapsuleCount
    keep = [RepFile("class Gone\n{\n    PROP(int v READWRITE);\n};\n")]
    twice = "class Twice\n{\n    PROP(int v READWRITE);\n};\n"
    first = RepFile(twice)      # noqa: F841
    before = getCapsuleCount()
    dropped = []

    def handler(mode, context, message):
        # Runs under the registry lock: the types of Gone go with the collection
        if keep:
            keep.clear()
            gc.collect()
            dropped.append(int(getCapsuleCount() < before))

    qInstallMessageHandler(handler)
    try:
        RepFile(twice)          # the name is in use: Qt warns under the lock
    except RuntimeError:
        pass
    qInstallMessageHandler(None)
    print(f"dropped {dropped[0] if dropped else 0}", flush=True)


def _types(threads=8, each=200):
    barrier = threading.Barrier(threads)
    errors = []

    def make(tag):
        barrier.wait()
        try:
            for i in range(each):
                RepFile(f"class H{tag}_{i}\n{{\n    PROP(int v{i} READWRITE);\n}};\n")
        except BaseException as e:      # noqa: B036
            errors.append(repr(e))

    makers = [threading.Thread(target=make, args=(t,)) for t in range(threads)]
    for t in makers:
        t.start()
    for t in makers:
        t.join()
    print(f"errors {len(errors)} {errors[:1]}", flush=True)


def _stw(seconds=2.0):
    # A stop-the-world waits for a thread in a long C loop that does not check
    # the eval breaker; on Python 3.15 the __qualname__ setter of a type stops
    # the world (Shiboken option NewTypeQualname).
    import time
    n = 10_000_000
    while True:
        t = time.monotonic()
        sum(range(n))
        if time.monotonic() - t > seconds / 10:
            break
        n *= 2
    loop = (time.monotonic() - t) * 10
    n *= 10
    RepFile("class Warm\n{\n    PROP(int v READWRITE);\n};\n")
    started = threading.Event()

    def busy():
        started.set()
        sum(range(n))

    worker = threading.Thread(target=busy)
    worker.start()
    started.wait()
    time.sleep(0.05)
    t = time.monotonic()
    RepFile("class Stw\n{\n    PROP(int v READWRITE);\n};\n")
    took = time.monotonic() - t
    worker.join()
    print(f"waited {int(took > loop / 4)} ({took:.3f}s, loop {loop:.1f}s)", flush=True)


def _property(rounds=20000):
    source = RepFile(CONTENTS).source["Holder"]()
    values = ["x" * 100 + str(i) for i in range(8)]
    source.s = values[0]
    stop = threading.Event()
    wrong = []

    def read():
        while not stop.is_set():
            try:
                if source.s not in values:
                    wrong.append(1)
            except Exception:       # a torn read may raise ("Invalid meta type")
                wrong.append(1)

    reader = threading.Thread(target=read)
    reader.start()
    for i in range(rounds):
        source.s = values[i % len(values)]
    stop.set()
    reader.join()
    print(f"wrong {len(wrong)}", flush=True)


if __name__ == '__main__':
    if "--property" in sys.argv:
        _property()
        sys.exit(0)
    if "--types" in sys.argv:
        _types()
        sys.exit(0)
    if "--stw" in sys.argv:
        _stw()
        sys.exit(0)
    if "--warning" in sys.argv:
        _warning()
        sys.exit(0)
    unittest.main()
