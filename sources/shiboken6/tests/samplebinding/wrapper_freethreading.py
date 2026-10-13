# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Wrapper lookups and the instance dict against a second thread.'''

import gc
import os
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


if __name__ == '__main__':
    unittest.main()
