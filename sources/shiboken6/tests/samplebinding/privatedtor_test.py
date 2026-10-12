#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for a class with a private destructor.'''

import gc
import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from shiboken6 import Shiboken
from sample import PrivateDtor, DeletedDtor


class PrivateDtorTest(unittest.TestCase):
    '''Test case for PrivateDtor class'''

    def assertRaises(self, *args, **kwds):
        if not hasattr(sys, "pypy_version_info"):
            # PYSIDE-535: PyPy complains "Fatal RPython error: NotImplementedError"
            return super().assertRaises(*args, **kwds)

    def testPrivateDtorInstanciation(self):
        '''Test if instanciation of class with a private destructor raises an exception.'''
        self.assertRaises(TypeError, PrivateDtor)

    def testPrivateDtorInheritance(self):
        '''Test if inheriting from PrivateDtor raises an exception.'''
        def inherit():
            class Foo(PrivateDtor):
                pass
        self.assertRaises(TypeError, inherit)

    def testPrivateDtorInstanceMethod(self):
        '''Test if PrivateDtor.instance() method return the proper singleton.'''
        pd1 = PrivateDtor.instance()
        calls = pd1.instanceCalls()
        self.assertEqual(type(pd1), PrivateDtor)
        pd2 = PrivateDtor.instance()
        self.assertEqual(pd2, pd1)
        self.assertEqual(pd2.instanceCalls(), calls + 1)

    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testPrivateDtorRefCounting(self):
        '''Test refcounting of the singleton returned by PrivateDtor.instance().'''
        pd1 = PrivateDtor.instance()
        calls = pd1.instanceCalls()
        refcnt = sys.getrefcount(pd1)
        pd2 = PrivateDtor.instance()
        self.assertEqual(pd2.instanceCalls(), calls + 1)
        self.assertEqual(sys.getrefcount(pd2), sys.getrefcount(pd1))
        self.assertEqual(sys.getrefcount(pd2), refcnt + 1)
        del pd1
        self.assertEqual(sys.getrefcount(pd2), refcnt)
        del pd2
        gc.collect()
        pd3 = PrivateDtor.instance()
        self.assertEqual(type(pd3), PrivateDtor)
        self.assertEqual(pd3.instanceCalls(), calls + 2)
        self.assertEqual(sys.getrefcount(pd3), refcnt)

    @unittest.skipIf(sysconfig.get_config_var("Py_GIL_DISABLED"),
                     "free-threaded: see refcount_freethreading.py")
    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testClassDecref(self):
        # Bug was that class PyTypeObject wasn't decrefed when instance
        # was invalidated

        before = sys.getrefcount(PrivateDtor)

        for i in range(1000):
            obj = PrivateDtor.instance()
            Shiboken.invalidate(obj)

        after = sys.getrefcount(PrivateDtor)

        self.assertLess(abs(before - after), 5)

    def testDeletedDtor(self):
        d = DeletedDtor()
        self.assertEqual(d.value(), 42)


if __name__ == '__main__':
    unittest.main()
