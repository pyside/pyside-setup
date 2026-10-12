# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Reference counting tests in the form a free-threaded build can measure.'''

import gc
import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths  # noqa: E402
init_paths()

from sample import ObjectType, PointerHolder, PrivateDtor  # noqa: E402
from shiboken6 import Shiboken  # noqa: E402


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class RefcountFreeThreadedTest(unittest.TestCase):

    def testObjectTypeClassDecref(self):
        # Bug was that class PyTypeObject wasn't decrefed when instance died
        before = sys.getrefcount(ObjectType)

        for i in range(1000):
            obj = ObjectType()
            Shiboken.delete(obj)
        del obj

        # Settle the deferred type references first
        gc.collect()
        after = sys.getrefcount(ObjectType)

        self.assertLessEqual(abs(before - after), 1)

    def testPointerHolderReferenceCounting(self):
        '''Test reference counting of PointerHolder.pointer().'''
        # Built at runtime: a constant tuple is immortal here
        a = tuple([1, 2, 3])
        refcnt = sys.getrefcount(a)
        ph = PointerHolder(a)
        ptr = ph.pointer()  # noqa: F841
        self.assertEqual(sys.getrefcount(a), refcnt + 1)

    def testPrivateDtorClassDecref(self):
        # Bug was that class PyTypeObject wasn't decrefed when instance
        # was invalidated

        before = sys.getrefcount(PrivateDtor)

        for i in range(1000):
            obj = PrivateDtor.instance()
            Shiboken.invalidate(obj)
        del obj

        # Settle the deferred type references first
        gc.collect()
        after = sys.getrefcount(PrivateDtor)

        self.assertLessEqual(abs(before - after), 1)


if __name__ == '__main__':
    unittest.main()
