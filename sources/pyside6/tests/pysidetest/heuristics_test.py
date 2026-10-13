# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import os
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(True)

import shiboken6
from testbinding import TestWidget
from helper.usesqapplication import UsesQApplication


class HeuristicsTest(UsesQApplication):
    """PYSIDE-3525: Verify that return value heuristics do not create
       reference cycles."""
    def testHeuristics(self):
        parent = TestWidget()
        TestWidget(parent)
        child = parent.heuristicChildWidget()
        parent2 = child.heuristicParentWidget()
        if sysconfig.get_config_var("Py_GIL_DISABLED"):
            # dumpTree() is refused in free-threaded builds; dump() shows the parent
            self.assertIn("parent....", shiboken6.dump(child))
            self.assertNotIn("parent....", shiboken6.dump(parent2))
            return
        # dumpTree() crashes when encountering a cycle
        self.assertTrue(shiboken6.dumpTree(parent2))


if __name__ == '__main__':
    unittest.main()
