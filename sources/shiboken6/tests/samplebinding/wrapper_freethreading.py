# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Wrapper lookups and the instance dict against a second thread.'''

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
from shiboken_paths import init_paths  # noqa: E402
init_paths()

from sample import ObjectType, PrivateDtor  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")


def wait_reached(name, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if Shiboken.failpointReached(name):
            return True
        time.sleep(0.001)
    return False


@unittest.skipUnless(FREE_THREADED, "free-threaded builds only")
class WrapperFreeThreadedTest(unittest.TestCase):

    def tearDown(self):
        if HAS_HOOKS:
            Shiboken.clearFailpoints()
        gc.enable()

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testDictFirstAccess(self):
        '''An attribute set during the first __dict__ access stays.'''
        obj = ObjectType()
        self.assertTrue(Shiboken.armFailpoint("dict-before-publish"))
        reader = threading.Thread(target=lambda: obj.__dict__)
        reader.start()
        self.assertTrue(wait_reached("dict-before-publish"))
        obj.attr = 42          # CPython creates the dict meanwhile
        Shiboken.releaseFailpoint("dict-before-publish")
        reader.join()
        self.assertEqual(getattr(obj, "attr", None), 42, "the instance dict was replaced")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testLookupMeetsDyingWrapper(self):
        '''A lookup does not revive a dying wrapper.'''
        gc.disable()
        made, go = threading.Event(), threading.Event()
        first = []

        def dropper():
            # Made here, so this thread deallocates (biased refcounting); C++ owns
            # the object and nothing in the binding holds the wrapper
            wrapper = PrivateDtor.instance()
            first.append(id(wrapper))
            made.set()
            go.wait()
            del wrapper

        thread = threading.Thread(target=dropper)
        thread.start()
        made.wait()
        self.assertTrue(Shiboken.armFailpoint("dealloc-before-unregister"))
        go.set()
        self.assertTrue(wait_reached("dealloc-before-unregister"))
        again = PrivateDtor.instance()
        revived = id(again) == first[0]
        Shiboken.releaseFailpoint("dealloc-before-unregister")
        thread.join()
        self.assertFalse(revived, "a lookup revived a dying wrapper")
        for _ in range(100):
            again.instanceCalls()
        self.assertTrue(Shiboken.isValid(again))

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testTwoThreadsConvertOneObject(self):
        '''Two threads converting one C++ object get one wrapper.'''
        gc.disable()
        parent = ObjectType.createWithChild()
        out = {}
        self.assertTrue(Shiboken.armFailpoint("register-after-lookup"))
        first = threading.Thread(target=lambda: out.setdefault("a", parent.children()[0]))
        first.start()
        self.assertTrue(wait_reached("register-after-lookup"))
        second = threading.Thread(target=lambda: out.setdefault("b", parent.children()[0]))
        second.start()
        time.sleep(0.3)        # the second either waits for the map or registers its own
        Shiboken.releaseFailpoint("register-after-lookup")
        first.join()
        second.join()
        self.assertIs(out["a"], out["b"], "one C++ object got two wrappers")

    def testDumpTreeRefused(self):
        '''dumpTree() is refused: it would read the graph without the lock.'''
        with self.assertRaises(NotImplementedError):
            Shiboken.dumpTree(ObjectType())

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testFirstMultipleInheritanceFromTwoThreads(self):
        '''First objects with two C++ bases in two threads get correct offsets.'''
        broken = "the base offsets were read half computed"
        try:
            proc = subprocess.run([sys.executable, __file__, "--mi-offsets"],
                                  capture_output=True, text=True, timeout=20)
        except subprocess.TimeoutExpired:
            self.fail(f"{broken}: the run hangs")
        tail = "\n".join(proc.stderr.splitlines()[-20:])
        self.assertEqual(proc.returncode, 0, f"{broken}: rc {proc.returncode}\n{tail}")
        self.assertIn("result ok", proc.stdout, f"{broken}\n{tail}")

    def child(self, option, broken, env=None):
        try:
            proc = subprocess.run([sys.executable, __file__, option], capture_output=True,
                                  text=True, timeout=20, env=env)
        except subprocess.TimeoutExpired:
            self.fail(f"{broken}: the run hangs")
        tail = "\n".join(proc.stderr.splitlines()[-20:])
        self.assertEqual(proc.returncode, 0, f"{broken}: rc {proc.returncode}\n{tail}")
        self.assertIn("result ok", proc.stdout, f"{broken}\n{tail}")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testWalkWhileAWrapperGoes(self):
        '''A walk over all wrappers holds one that another thread drops.'''
        self.child("--walk", "the walk took a wrapper that was gone")

    @unittest.skipUnless(HAS_HOOKS, "needs the failpoint hooks of a debug build")
    def testDumpWhileWrappersGo(self):
        '''dumpWrapperMap() while wrappers go reads no freed node.'''
        self.child("--dump", "the wrapper map changed under its dump",
                   dict(os.environ, MallocScribble="1"))


def _walk():
    gc.disable()
    victim = ObjectType()
    victim.setObjectName("victim")
    got = []
    assert Shiboken.armFailpoint("walk-after-collect")
    walker = threading.Thread(target=lambda: got.append(Shiboken.getAllValidWrappers()))
    walker.start()
    assert wait_reached("walk-after-collect")
    del victim                  # its last reference, unless the walk holds one
    ballast = []
    for _ in range(2000):
        obj = ObjectType()
        obj.setObjectName("ballast")
        ballast.append(obj)
    Shiboken.releaseFailpoint("walk-after-collect")
    walker.join()
    # The walk collected before any ballast existed
    names = {str(o.objectName()) for o in got[0] if isinstance(o, ObjectType)}
    print(f"result {'ok' if 'ballast' not in names else names}", flush=True)


def _dump():
    gc.disable()
    victims = [ObjectType() for _ in range(5)]
    assert Shiboken.armFailpoint("dump-mid-walk")
    dumper = threading.Thread(target=Shiboken.dumpWrapperMap)
    dumper.start()
    assert wait_reached("dump-mid-walk")
    dropper = threading.Thread(target=victims.clear)
    dropper.start()
    dropper.join(0.5)           # with the map locked it waits for the dump
    Shiboken.releaseFailpoint("dump-mid-walk")
    dumper.join()
    dropper.join()
    print("result ok", flush=True)


def _map_size():
    """The wrapper map's size, from dumpWrapperMap(), which writes to fd 2."""
    import re
    import tempfile
    with tempfile.TemporaryFile() as out:
        saved = os.dup(2)
        os.dup2(out.fileno(), 2)
        try:
            Shiboken.dumpWrapperMap()
        finally:
            os.dup2(saved, 2)
            os.close(saved)
        out.seek(0)
        return int(re.search(rb"WrapperMap size: (\d+)", out.read()).group(1))


def _mi_offsets():
    from sample import MDerived1
    made = []
    gc.disable()        # the second waits attached on the static's guard
    before = _map_size()
    assert Shiboken.armFailpoint("mi-offsets-mid-fill")
    first = threading.Thread(target=lambda: made.append(MDerived1()))
    first.start()
    assert wait_reached("mi-offsets-mid-fill")
    second = threading.Thread(target=lambda: made.append(MDerived1()))
    second.start()
    second.join(0.5)    # with the offsets computed once it waits for the first
    Shiboken.releaseFailpoint("mi-offsets-mid-fill")
    first.join()
    second.join()
    # Each Base2 part is found under its own address, and none is left in the
    # map when gone. A half computed offset list (no -1 yet) registers addresses
    # its release does not know.
    found = [obj.castToBase2() is obj for obj in made]
    made.clear()
    left = _map_size() - before
    ok = found == [True, True] and left == 0
    print(f"result {'ok' if ok else (found, left)}", flush=True)


if __name__ == '__main__':
    if "--walk" in sys.argv:
        _walk()
        sys.exit(0)
    if "--dump" in sys.argv:
        _dump()
        sys.exit(0)
    if "--mi-offsets" in sys.argv:
        _mi_offsets()
        sys.exit(0)
    unittest.main()
