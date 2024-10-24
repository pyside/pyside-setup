#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(True)

from testbinding import PySideCPP, TestObjectWithoutNamespace


class ModelViewTest(unittest.TestCase):

    def callback(self, o):
        self._called = o

    def testWithoutNamespace(self):
        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignal.connect(self.callback)
        o.emitSignal.emit(o)
        self.assertTrue(o == self._called)

        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignal.connect(self.callback)
        o.callSignal(o)
        self.assertTrue(o == self._called)

    def testWithNamespace(self):
        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignalWithNamespace.connect(self.callback)
        o.emitSignalWithNamespace.emit(o)
        self.assertTrue(o == self._called)

        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignalWithNamespace.connect(self.callback)
        o.callSignalWithNamespace(o)
        self.assertTrue(o == self._called)

    def testWithoutNamespace1(self):
        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignal.connect(self.callback)
        o.emitSignal.emit(o)
        self.assertTrue(o == self._called)

        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignal.connect(self.callback)
        o.callSignal(o)
        self.assertTrue(o == self._called)

    def testWithNamespace1(self):
        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignalWithNamespace.connect(self.callback)
        o.emitSignalWithNamespace.emit(o)
        self.assertTrue(o == self._called)

        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignalWithNamespace.connect(self.callback)
        o.callSignalWithNamespace(o)
        self.assertTrue(o == self._called)

    def testTypedfWithouNamespace(self):
        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignalWithTypedef.connect(self.callback)
        o.emitSignalWithTypedef.emit(10)
        self.assertEqual(10, self._called)

        self._called = None
        o = PySideCPP.TestObjectWithNamespace(None)
        o.emitSignalWithTypedef.connect(self.callback)
        o.callSignalWithTypedef(10)
        self.assertEqual(10, self._called)

    def testTypedefWithNamespace(self):
        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignalWithTypedef.connect(self.callback)
        o.emitSignalWithTypedef.emit(10)
        self.assertEqual(10, self._called)

        self._called = None
        o = TestObjectWithoutNamespace(None)
        o.emitSignalWithTypedef.connect(self.callback)
        o.callSignalWithTypedef(10)
        self.assertEqual(10, self._called)


if __name__ == '__main__':
    unittest.main()

