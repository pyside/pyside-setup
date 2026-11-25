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

from helper.helper import quickview_errorstring
from helper.timedqguiapplication import TimedQGuiApplication
from PySide6.QtCore import QObject, QUrl, Slot
from PySide6.QtQuick import QQuickView
from PySide6.QtQml import QmlElement

QML_IMPORT_NAME = "test.ProxyObject"
QML_IMPORT_MAJOR_VERSION = 1


@QmlElement
class ProxyObject(QObject):
    def __init__(self):
        super().__init__()
        self._o = None
        self._receivedName = ""
        self.emit_none = False

    @Slot(result='QObject*')
    def getObject(self):
        if self.emit_none:
            return None

        if self._o:
            return self._o

        self._o = QObject()
        self._o.setObjectName("PySideObject")
        return self._o

    @Slot(str)
    def receivedObject(self, name):
        self._receivedName = name


class TestConnectionWithInvalidSignature(TimedQGuiApplication):

    def testSlotRetur(self):
        view = QQuickView()
        proxy = ProxyObject()

        view.setInitialProperties({"proxy": proxy})
        file = Path(__file__).resolve().parent / 'bug_726.qml'
        self.assertTrue(file.is_file())
        view.setSource(QUrl.fromLocalFile(file))
        root = view.rootObject()
        self.assertTrue(root, quickview_errorstring(view))
        button = root.findChild(QObject, "buttonMouseArea")
        view.show()
        button.entered.emit()
        self.assertEqual(proxy._receivedName, "PySideObject")

        # PYSIDE-3531: Test whether "None" is correctly converted to nullptr/undefined
        proxy.emit_none = True
        button.entered.emit()
        self.assertEqual(proxy._receivedName, "None")


if __name__ == '__main__':
    unittest.main()
