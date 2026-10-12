#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for a class that holds an arbitraty pointer and is modified to hold an PyObject.'''

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import PointerHolder


class TestPointerHolder(unittest.TestCase):
    '''Test cases for a class that holds an arbitraty pointer and
       is modified to hold an PyObject.'''

    def testStoringAndRetrievingPointer(self):
        ph = PointerHolder('Hello')
        self.assertEqual(ph.pointer(), 'Hello')
        a = (1, 2, 3)
        ph = PointerHolder(a)
        self.assertEqual(ph.pointer(), a)

    @unittest.skipIf(sysconfig.get_config_var("Py_GIL_DISABLED"),
                     "free-threaded: see refcount_freethreading.py")
    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testReferenceCounting(self):
        '''Test reference counting when retrieving data with PointerHolder.pointer().'''
        a = (1, 2, 3)
        refcnt = sys.getrefcount(a)
        ph = PointerHolder(a)
        ptr = ph.pointer()  # noqa: F841
        self.assertEqual(sys.getrefcount(a), refcnt + 1)


if __name__ == '__main__':
    unittest.main()
