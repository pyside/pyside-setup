# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
from __future__ import annotations

"""
feature_with_uic_test.py
------------------------

Check if feature switching works with a normal UIC file.
This crashed due to callbacks into QApplication.

PYSIDE-1626: Switch early in `BindingManager::getOverride`.
"""

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from helper.usesqapplication import UsesQApplication

from PySide6.QtCore import QCoreApplication, qVersion
from PySide6.QtWidgets import QMainWindow

# PYSIDE-535: We cannot use __feature__ in PyPy, yet
try:
    from __feature__ import snake_case  # noqa

    from feature_with_uic.window import Ui_MainWindow
    have_feature = True
except ImportError:
    Ui_MainWindow = object
    have_feature = False


class MainWindow(QMainWindow, Ui_MainWindow):

    def __init__(self) -> None:
        super().__init__()
        self.setupUi(self)


@unittest.skipIf(sysconfig.get_config_var("Py_GIL_DISABLED"),
                 "free-threaded builds have no __feature__")
@unittest.skipIf(hasattr(sys, "pypy_version_info"),
                 "__feature__ cannot yet be used with PyPy")
class FeatureTest(UsesQApplication):

    def testFeaturesWorkWithUIC(self):
        window = MainWindow()
        window.set_window_title(qVersion())
        window.show()
        while not window.window_handle().is_exposed():
            QCoreApplication.process_events()


if __name__ == '__main__':
    unittest.main()
