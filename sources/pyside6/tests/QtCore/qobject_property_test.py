# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for QObject property and setProperty'''

import os
import sys
import unittest

from pathlib import Path
from typing import NamedTuple
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject, QPoint, Property, Signal


Point = NamedTuple("Point", [("x", float), ("y", float)])


class EvilList(list):
    """A list whose __getitem__ raises."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.fail_row = 1

    def __getitem__(self, row):
        if row == self.fail_row:
            raise IndexError
        return super().__getitem__(row)


class MyObjectWithNotifyProperty(QObject):
    def __init__(self, parent=None):
        QObject.__init__(self, parent)
        self.p = 0

    def readP(self):
        return self.p

    def writeP(self, v):
        self.p = v
        self.notifyP.emit()

    notifyP = Signal()
    myProperty = Property(int, readP, fset=writeP, notify=notifyP)


class OtherClass:
    """Helper for QObjectWithOtherClassPropertyTest."""
    pass


class MyObjectWithOtherClassProperty(QObject):
    """Helper for QObjectWithOtherClassPropertyTest."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._otherclass = None

    def _get_otherclass(self):
        return self._otherclass

    def _set_otherclass(self, o):
        self._otherclass = o

    otherclass = Property(OtherClass, fget=_get_otherclass, fset=_set_otherclass)


class TestVariantPropertyObject(QObject):
    """Helper for testing QVariant conversion in properties and signals
       (PYSIDE-3206, PYSIDE-3244). It uses a property of list type that
       can passed a QVariant list with various element types."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._property = None

    def set_property(self, v):
        self._property = v

    def get_property(self):
        return self._property

    testProperty = Property(list, fget=get_property, fset=set_property)


class PropertyWithNotify(unittest.TestCase):
    def called(self):
        self.called_ = True

    def testNotify(self):
        self.called_ = False
        obj = MyObjectWithNotifyProperty()
        obj.notifyP.connect(self.called)
        obj.myProperty = 10
        self.assertTrue(self.called_)

    def testHasProperty(self):
        o = MyObjectWithNotifyProperty()
        o.setProperty("myProperty", 10)
        self.assertEqual(o.myProperty, 10)
        self.assertEqual(o.property("myProperty"), 10)


class QObjectWithOtherClassPropertyTest(unittest.TestCase):
    """PYSIDE-2193: For properties of custom classes not wrapped by shiboken,
       QVariant<PyObjectWrapper> is used, which had refcount issues causing crashes.
       Exercise the QVariant conversion by setting and retrieving via the
       QVariant-based property()/setProperty() API."""
    def testNotify(self):
        obj = MyObjectWithOtherClassProperty()
        obj.setProperty("otherclass", OtherClass())
        for i in range(10):
            pv = obj.property("otherclass")
            print(pv)  # Exercise repr
            self.assertTrue(type(pv) is OtherClass)


class VariantPropertyTest(unittest.TestCase):
    """Test QVariant conversion in properties and signals (PYSIDE-3256,
       PYSIDE-3244, PYSIDE-3206 [open]). It uses a property of list type
       that is passed a QVariantList with various element types when
       using QObject.setProperty()."""

    def testIt(self):
        to = TestVariantPropertyObject()
        idx = to.metaObject().indexOfProperty("testProperty")
        self.assertTrue(idx != -1)

        # List
        to.setProperty("testProperty", [[1, 2]])
        self.assertEqual(type(to.get_property()[0]), list)

        # Dict
        to.setProperty("testProperty", [{"key": 42}])
        self.assertEqual(type(to.get_property()[0]), dict)

        # Tuple (PYSIDE-3256)
        to.setProperty("testProperty", [(1, 2)])
        self.assertEqual(type(to.get_property()[0]), tuple)

        # Named Tuple (PYSIDE-3244)
        to.setProperty("testProperty", [Point(1, 2)])
        self.assertEqual(type(to.get_property()[0]), Point)

    def testGetItemRaises(self):
        """Ensure there is no crash when __getitem__ raises."""
        to = TestVariantPropertyObject()
        with self.assertRaises(IndexError):
            el = EvilList([1, 2])
            to.setProperty("testProperty", el)

        with self.assertRaises(IndexError):
            el = EvilList(["s1", "s2"])  # Trigger the isStringList check
            to.setProperty("testProperty", el)

        with self.assertRaises(IndexError):
            el = EvilList([QPoint(1, 2), QPoint(1, 2)])  # convertToValueList() path
            el.fail_row = 0
            to.setProperty("testProperty", el)


if __name__ == '__main__':
    unittest.main()
