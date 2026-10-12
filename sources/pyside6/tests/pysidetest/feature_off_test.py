# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import importlib
import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject  # noqa: E402


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class FeatureOffTest(unittest.TestCase):
    """A free-threaded build has no __feature__, by any path."""

    def testFromImportRefused(self):
        with self.assertRaises(ImportError) as cm:
            exec("from __feature__ import snake_case", {"__name__": "feature_off_probe"})
        self.assertIn("free-threaded", str(cm.exception))

    def testModuleNotPublished(self):
        with self.assertRaises(ImportError):
            importlib.import_module("__feature__")

    def testSelectionHasNoEffect(self):
        from PySide6.support import feature
        feature.set_selection(feature.snake_case)
        self.assertFalse(hasattr(QObject, "object_name"))


if __name__ == '__main__':
    unittest.main()
