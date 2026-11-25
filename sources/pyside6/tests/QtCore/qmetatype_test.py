#!/usr/bin/python
# Copyright (C) 2022The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Tests for QMetaType'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import (QMetaType, QObject, QPoint)


class qmetatype_test(unittest.TestCase):
    def test_ObjectSlotSignal(self):
        meta_type = QMetaType(int)
        self.assertTrue(meta_type.isValid())
        self.assertEqual(meta_type.name(), "int")

        meta_type = QMetaType(str)
        self.assertTrue(meta_type.isValid())
        self.assertEqual(meta_type.name(), "QString")

        meta_type = QMetaType(float)
        self.assertTrue(meta_type.isValid())
        self.assertEqual(meta_type.name(), "double")

        meta_type = QMetaType(QPoint)
        self.assertTrue(meta_type.isValid())
        self.assertEqual(meta_type.name(), "QPoint")

        meta_type = QMetaType(QObject)
        self.assertTrue(meta_type.isValid())
        self.assertEqual(meta_type.name(), "QObject*")
        self.assertEqual(meta_type.metaObject().className(), "QObject")


if __name__ == '__main__':
    unittest.main()
