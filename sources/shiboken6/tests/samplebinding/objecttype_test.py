#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Tests ObjectType class of object-type with privates copy constructor and = operator.'''

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()
import sys

from sample import ObjectType, Str
from shiboken6 import Shiboken


class ObjectTypeTest(unittest.TestCase):
    '''Test cases  ObjectType class of object-type with privates copy constructor and = operator.'''

    def testObjectTypeSetObjectNameWithStrVariable(self):
        '''ObjectType.setObjectName with Str variable as argument.'''
        s = Str('object name')
        o = ObjectType()
        o.setObjectName(s)
        self.assertEqual(str(o.objectName()), str(s))

    def testObjectTypeSetObjectNameWithStrInstantiation(self):
        '''ObjectType.setObjectName with Str instantiation as argument.'''
        s = 'object name'
        o = ObjectType()
        o.setObjectName(Str(s))
        self.assertEqual(str(o.objectName()), s)

    def testObjectTypeSetObjectNameWithPythonString(self):
        '''ObjectType.setObjectName with Python string as argument.'''
        o = ObjectType()
        o.setObjectName('object name')
        self.assertEqual(str(o.objectName()), 'object name')

    def testNullOverload(self):
        o = ObjectType()
        o.setObject(None)
        self.assertEqual(o.callId(), 0)
        o.setNullObject(None)
        self.assertEqual(o.callId(), 1)

    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testParentFromCpp(self):
        o = ObjectType()
        base_count = sys.getrefcount(o)  # 1 from 3.14 onwards, previously 2
        o.getCppParent().setObjectName('parent')
        self.assertEqual(sys.getrefcount(o), base_count + 1)
        o.getCppParent().setObjectName('parent')
        self.assertEqual(sys.getrefcount(o), base_count + 1)
        o.getCppParent().setObjectName('parent')
        self.assertEqual(sys.getrefcount(o), base_count + 1)
        o.getCppParent().setObjectName('parent')
        self.assertEqual(sys.getrefcount(o), base_count + 1)
        o.getCppParent().setObjectName('parent')
        self.assertEqual(sys.getrefcount(o), base_count + 1)
        o.destroyCppParent()
        self.assertEqual(sys.getrefcount(o), base_count)

    def testNextInFocusChainCycle(self):
        parent = ObjectType()
        child = ObjectType(parent)
        next_focus = child.nextInFocusChain()  # noqa: F841

        Shiboken.invalidate(parent)

    def testNextInFocusChainCycleList(self):
        '''As above but in for a list of objects'''
        parents = []
        children = []
        focus_chains = []
        for i in range(10):
            parent = ObjectType()
            child = ObjectType(parent)
            next_focus = child.nextInFocusChain()
            parents.append(parent)
            children.append(child)
            focus_chains.append(next_focus)

        Shiboken.invalidate(parents)

    @unittest.skipIf(sysconfig.get_config_var("Py_GIL_DISABLED"),
                     "free-threaded: see refcount_freethreading.py")
    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testClassDecref(self):
        # Bug was that class PyTypeObject wasn't decrefed when instance died
        before = sys.getrefcount(ObjectType)

        for i in range(1000):
            obj = ObjectType()
            Shiboken.delete(obj)

        after = sys.getrefcount(ObjectType)

        self.assertLess(abs(before - after), 5)

    def testInvalidProperty(self):
        o = ObjectType()
        with self.assertRaises(AttributeError):
            o.typo


if __name__ == '__main__':
    unittest.main()
