#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QFile
from PySide6.QtGui import QGuiApplication
from PySide6.QtSvg import QSvgRenderer


class QSvgRendererTest(unittest.TestCase):

    def testLoad(self):
        tigerPath = os.path.join(os.path.dirname(__file__), 'tiger.svg')
        app = QGuiApplication([])

        fromFile = QSvgRenderer(tigerPath)
        self.assertTrue(fromFile.isValid())

        tigerFile = QFile(tigerPath)
        tigerFile.open(QFile.ReadOnly)
        tigerData = tigerFile.readAll()
        fromContents = QSvgRenderer(tigerData)
        self.assertTrue(fromContents.isValid())


if __name__ == '__main__':
    unittest.main()

