# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from helper.usesqguiapplication import UsesQGuiApplication
from PySide6.QtCore import QSize
from PySide6.QtGui import QBitmap, QImage


class TestQBitmap(UsesQGuiApplication):
    def testFromDataMethod(self):
        dataBits = bytes('\x38\x28\x38\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\xfe\xfe\x7c\x7c\x38\x38\x10\x10', "UTF-8")
        bim = QBitmap.fromData(QSize(8, 48), dataBits, QImage.Format_Mono)  # missing function


if __name__ == '__main__':
    unittest.main()
