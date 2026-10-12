# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths  # noqa: E402
init_test_paths(False)

from PySide6 import QtCore
from PySide6.QtWidgets import QApplication, QLabel

is_pypy = hasattr(sys, "pypy_version_info")
is_ft = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
if not is_pypy:
    from PySide6.support import feature  # noqa: E402

import inspect

"""
errormessages_with_features_test.py
-----------------------------------

When errors occur while features are switched, we must always produce a
valid error message.

This test is in its own file because combining it with
"snake_prop_feature_test" gave strange interactions with the other tests.
"""


@unittest.skipIf(is_ft, "free-threaded builds have no __feature__")
@unittest.skipIf(is_pypy, "__feature__ cannot yet be used with PyPy")
class ErrormessagesWithFeatures(unittest.TestCase):
    probe = "called with wrong argument types"

    def setUp(self):
        qApp or QApplication()  # noqa
        feature.reset()

    def tearDown(self):
        feature.reset()
        qApp.shutdown()  # noqa

    def testCorrectErrorMessagesPlain(self):
        with self.assertRaises(TypeError) as cm:
            QLabel().setFont(42)
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])

    def testCorrectErrorMessagesSnake(self):
        from __feature__ import snake_case  # noqa
        with self.assertRaises(TypeError) as cm:
            QLabel().set_font(42)
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])

    def testCorrectErrorMessagesProp(self):
        from __feature__ import true_property  # noqa
        with self.assertRaises(TypeError) as cm:
            QLabel().font = 42
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])

    def testCorrectErrorMessagesSnakeProp(self):
        from __feature__ import snake_case, true_property  # noqa
        with self.assertRaises(TypeError) as cm:
            QLabel().font = 42
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])

    def testCorrectErrorMessagesClassProp(self):
        from __feature__ import true_property  # noqa
        with self.assertRaises(TypeError) as cm:
            QApplication.quitOnLastWindowClosed = object
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])
        with self.assertRaises(TypeError) as cm:
            qApp.quitOnLastWindowClosed = object  # noqa
        self.assertTrue(self.probe in cm.exception.args[0])

    def testCorrectErrorMessagesClassSnakeProp(self):
        from __feature__ import snake_case, true_property  # noqa
        with self.assertRaises(TypeError) as cm:
            QApplication.quit_on_last_window_closed = object  # noqa
        print("\n\n" + cm.exception.args[0])
        self.assertTrue(self.probe in cm.exception.args[0])
        with self.assertRaises(TypeError) as cm:
            qApp.quit_on_last_window_closed = object  # noqa: F821
        self.assertTrue(self.probe in cm.exception.args[0])

    def testDocIsWorking(self):
        """
        make sure that it does not crash when touched
        """
        inspect.getdoc(QApplication)
        inspect.getdoc(QtCore)


if __name__ == '__main__':
    unittest.main()
