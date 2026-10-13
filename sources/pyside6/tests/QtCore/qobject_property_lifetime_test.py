# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''A QObject stored in a dynamic property is kept alive.'''

import gc
import os
import sys
import sysconfig
import unittest
import weakref

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class DynamicPropertyLifetimeTest(unittest.TestCase):
    """A QObject stored in a dynamic property stays alive."""

    def testStoredObjectIsKeptAlive(self):
        holder, value = QObject(), QObject()
        value.setObjectName("kept")
        holder.setProperty("x", value)
        del value
        self.assertEqual(holder.property("x").objectName(), "kept")

    def testStoredObjectInContainerIsKeptAlive(self):
        holder, value = QObject(), QObject()
        value.setObjectName("kept")
        holder.setProperty("x", [value])
        del value
        self.assertEqual(holder.property("x")[0].objectName(), "kept")

    def testReplacedValueIsLetGo(self):
        holder, value = QObject(), QObject()
        holder.setProperty("x", value)
        seen = weakref.ref(value)
        del value
        gc.collect()
        self.assertIsNotNone(seen(), "the property must hold the value")
        holder.setProperty("x", 42)
        gc.collect()
        self.assertIsNone(seen(), "the old value must be released")

    def testRemovedPropertyIsLetGo(self):
        holder, value = QObject(), QObject()
        holder.setProperty("x", value)
        seen = weakref.ref(value)
        del value
        gc.collect()
        self.assertIsNotNone(seen(), "the property must hold the value")
        holder.setProperty("x", None)
        gc.collect()
        self.assertIsNone(seen(), "the old value must be released")


if __name__ == '__main__':
    unittest.main()
