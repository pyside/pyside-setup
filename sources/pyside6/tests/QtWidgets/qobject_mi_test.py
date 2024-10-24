# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for multiple inheritance from 2 QObjects'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject
from PySide6.QtGui import QIntValidator, QValidator
from PySide6.QtWidgets import QWidget

from helper.usesqapplication import UsesQApplication


class WidgetValidator(QWidget, QIntValidator):
    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        QIntValidator.__init__(self, parent)


class DoubleQObjectInheritanceTest(UsesQApplication):

    def testDouble(self):
        '''Double inheritance from QObject classes'''

        obj = WidgetValidator()

        # QObject methods
        obj.setObjectName('aaaa')
        self.assertEqual(obj.objectName(), 'aaaa')

        # QWidget methods
        obj.setVisible(False)
        self.assertFalse(obj.isVisible())

        # QIntValidator methods
        state, string, number = obj.validate('aaaa', 0)
        self.assertEqual(state, QValidator.Invalid)
        state, string, number = obj.validate('33', 0)
        self.assertEqual(state, QValidator.Acceptable)


if __name__ == '__main__':
    unittest.main()
