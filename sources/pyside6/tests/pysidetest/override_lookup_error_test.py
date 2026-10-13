# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QCoreApplication, QEvent, QObject  # noqa: E402


class FailingLookup(QObject):
    calls = 0
    fail = False

    def __getattribute__(self, name):
        if name == "event" and type(self).fail:
            type(self).fail = False
            raise RuntimeError("the lookup fails once")
        return super().__getattribute__(name)

    def event(self, e):
        type(self).calls += 1
        return super().event(e)


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class OverrideLookupErrorTest(unittest.TestCase):
    """Test that a failed override lookup is not cached."""

    def testOverrideAfterFailedLookup(self):
        app = QCoreApplication.instance() or QCoreApplication([])  # noqa: F841
        obj = FailingLookup()
        FailingLookup.fail = True
        try:
            QCoreApplication.sendEvent(obj, QEvent(QEvent.Type.User))
        except RuntimeError:
            pass
        QCoreApplication.sendEvent(obj, QEvent(QEvent.Type.User))
        self.assertEqual(FailingLookup.calls, 1, "the override stayed disabled")


if __name__ == '__main__':
    unittest.main()
