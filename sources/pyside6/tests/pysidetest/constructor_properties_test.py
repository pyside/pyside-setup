# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from helper.usesqapplication import UsesQApplication
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QAction
from PySide6.QtWidgets import QApplication, QLabel, QFrame

is_pypy = hasattr(sys, "pypy_version_info")
is_ft = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
if not is_pypy:
    from PySide6.support import feature


class ConstructorPropertiesTest(unittest.TestCase):

    def setUp(self):
        qApp or QApplication()  # noqa: F821
        if not is_pypy:
            feature.reset()

    def tearDown(self):
        if not is_pypy:
            feature.reset()
        qApp.shutdown()  # noqa: F821

    # PYSIDE-1019: First property extension was support by the constructor.
    def testCallConstructor(self):
        label = QLabel(
            frameStyle=QFrame.Shape.Panel | QFrame.Shadow.Sunken,  # QFrame attr, no property
            lineWidth=2,                                           # QFrame property
            text="first line\nsecond line",                        # QLabel property
            alignment=Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight  # QLabel property
        )
        self.assertEqual(label.lineWidth(), 2)
        self.assertRaises(AttributeError, lambda: QLabel(
            somethingelse=42,
        ))

    # PYSIDE-1705: The same with snake_case
    @unittest.skipIf(is_ft, "free-threaded builds have no __feature__")
    @unittest.skipIf(is_pypy, "feature switching is not yet possible in PyPy")
    def testCallConstructor_snake(self):
        from __feature__ import snake_case  # noqa: F401

        label = QLabel(
            frame_style=QFrame.Shape.Panel | QFrame.Shadow.Sunken,  # QFrame attr, no property
            line_width=2,                                           # QFrame property
            text="first line\nsecond line",                         # QLabel property
            alignment=Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight  # QLabel property
        )
        self.assertEqual(label.line_width(), 2)
        self.assertRaises(AttributeError, lambda: QLabel(
            lineWidth=2,                                # QFrame property
        ))

    # PYSIDE-1705: The same with true_property
    @unittest.skipIf(is_ft, "free-threaded builds have no __feature__")
    @unittest.skipIf(is_pypy, "feature switching is not yet possible in PyPy")
    def testCallConstructor_prop(self):
        from __feature__ import true_property  # noqa: F401

        label = QLabel(
            frameStyle=QFrame.Shape.Panel | QFrame.Shadow.Sunken,  # QFrame attr, no property
            lineWidth=2,                                           # QFrame property
            text="first line\nsecond line",                        # QLabel property
            alignment=Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight  # QLabel property
        )
        self.assertEqual(label.lineWidth, 2)
        self.assertRaises(AttributeError, lambda: QLabel(
            line_width=2,                               # QFrame property
        ))

    # PYSIDE-1705: The same with snake_case and true_property
    @unittest.skipIf(is_ft, "free-threaded builds have no __feature__")
    @unittest.skipIf(is_pypy, "feature switching is not yet possible in PyPy")
    def testCallConstructor_prop_snake(self):
        from __feature__ import snake_case, true_property  # noqa: F401

        label = QLabel(
            frame_style=QFrame.Shape.Panel | QFrame.Shadow.Sunken,   # QFrame attr, no property
            line_width=2,                               # QFrame property
            text="first line\nsecond line",             # QLabel property
            alignment=Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight  # QLabel property
        )
        self.assertEqual(label.line_width, 2)
        self.assertRaises(AttributeError, lambda: QLabel(
            lineWidth=2,                                # QFrame property
        ))


class DiverseKeywordsTest(UsesQApplication):

    def testDuplicateKeyword(self):
        r, g, b, a = 1, 2, 3, 4
        with self.assertRaises(TypeError) as cm:
            QColor(r, g, b, a, a=0)
        self.assertTrue("multiple" in cm.exception.args[0])

    # PYSIDE-1305: Handle keyword args correctly.
    def testUndefinedKeyword(self):
        r, g, b, a = 1, 2, 3, 4
        # From the jira issue:
        with self.assertRaises(AttributeError) as cm:
            QColor(r, g, b, a, alpha=0)
        self.assertTrue("unsupported" in cm.exception.args[0])

    # PYSIDE-1305: Handle keyword args correctly.
    def testUndefinedConstructorKeyword(self):
        # make sure that the given attribute lands in the constructor
        x = QAction(autoRepeat=False)
        self.assertEqual(x.autoRepeat(), False)
        x = QAction(autoRepeat=True)
        self.assertEqual(x.autoRepeat(), True)
        # QAction derives from QObject, and so the missing attributes
        # in the constructor are reported as AttributeError.
        with self.assertRaises(AttributeError):
            QAction(some_other_name=42)


if __name__ == '__main__':
    unittest.main()
