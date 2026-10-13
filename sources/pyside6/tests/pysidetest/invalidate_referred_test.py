# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''An invalidated object takes its children with it, not what it refers to
(PYSIDE-3468).'''

import gc
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
class InvalidateReferredTest(unittest.TestCase):

    def testSharedMetaObjectSurvivesAStranger(self):
        '''Deleting a parent leaves the shared meta object valid for others.'''
        meta = QObject().metaObject()
        parent = QObject()
        child = QObject(parent)
        child.metaObject()      # now referred to by the child as well
        del parent              # deletes the child's C++ object too
        gc.collect()
        try:
            name = meta.className()
        except RuntimeError:
            name = None
        self.assertEqual(name, "QObject", "the shared meta object was invalidated with a stranger")
        del child


if __name__ == '__main__':
    unittest.main()
