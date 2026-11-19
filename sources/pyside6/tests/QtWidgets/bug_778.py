# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from helper.usesqapplication import UsesQApplication

from PySide6.QtWidgets import QTreeWidget, QTreeWidgetItem, QTreeWidgetItemIterator


class QTreeWidgetItemIteratorTest(UsesQApplication):
    """Test QTreeWidget iteration. Note that QTreeWidget/QTreeModel internally
       keeps lists of iterators by pointer which it invalidates in clear(),
       so, special care must be taken to avoid leaks."""
    def testWidgetIterator(self):
        treeWidget = QTreeWidget()

        def run_test():
            items = []
            for i in range(10):
                items.append(QTreeWidgetItem(None, [f'item: {i}']))
            treeWidget.insertTopLevelItems(0, items)

            index = 0
            for it in QTreeWidgetItemIterator(treeWidget):
                self.assertEqual(it.value().text(0), f'item: {index}')
                index += 1

        treeWidget.setColumnCount(1)
        run_test()

        # PYSIDE-3521: Repeat, ensure no old iterators cause issues
        treeWidget.clear()
        treeWidget.setColumnCount(1)
        run_test()

        gc.collect()
        alive = sum(type(o).__name__ == 'QTreeWidgetItemIterator' for o in gc.get_objects())
        self.assertEqual(alive, 0)


if __name__ == '__main__':
    unittest.main()
