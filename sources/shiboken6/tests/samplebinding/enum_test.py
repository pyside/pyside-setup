#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for Python representation of C++ enums.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

# This is needed after the introduction of BUILD_DIR.

import sample
from sample import SampleNamespace, ObjectType, Event


def createTempFile():
    import tempfile
    return tempfile.SpooledTemporaryFile(mode='rw')


class EnumTest(unittest.TestCase):
    '''Test case for Python representation of C++ enums.'''

    def testHashability(self):
        self.assertEqual(hash(SampleNamespace.InValue.TwoIn), hash(SampleNamespace.OutValue.TwoOut))
        self.assertNotEqual(hash(SampleNamespace.InValue.TwoIn),
                            hash(SampleNamespace.InValue.OneIn))

    def testEnumValuesInsideEnum(self):
        '''Enum values should be accessible inside the enum as well as outside.'''
        for value_name in SampleNamespace.Option.__members__:
            enum_item1 = getattr(SampleNamespace.Option, value_name)
            enum_item2 = getattr(SampleNamespace, value_name)
            self.assertEqual(enum_item1, enum_item2)

    def testPassingIntegerOnEnumArgument(self):
        '''Tries to use an integer in place of an enum argument.'''
        self.assertRaises(TypeError, SampleNamespace.getNumber, 1)

    def testBuildingEnumFromIntegerValue(self):
        '''Tries to build the proper enum using an integer.'''
        SampleNamespace.getNumber(SampleNamespace.Option(1))

    def testBuildingEnumWithDefaultValue(self):
        '''Enum constructor with default value'''
        enum = SampleNamespace.Option()
        self.assertEqual(enum, SampleNamespace.Option.None_)

    def testEnumConversionToAndFromPython(self):
        '''Conversion of enum objects from Python to C++ back again.'''
        enumout = SampleNamespace.enumInEnumOut(SampleNamespace.InValue.TwoIn)
        self.assertTrue(enumout, SampleNamespace.OutValue.TwoOut)
        self.assertEqual(repr(enumout), repr(SampleNamespace.OutValue.TwoOut))

    def testEnumConstructorWithTooManyParameters(self):
        '''Calling the constructor of non-extensible enum with the wrong number of parameters.'''
        self.assertRaises((TypeError, ValueError), SampleNamespace.InValue, 13, 14)

    def testEnumConstructorWithNonNumberParameter(self):
        '''Calling the constructor of non-extensible enum with a string.'''
        self.assertRaises((TypeError, ValueError), SampleNamespace.InValue, '1')

    def testEnumItemAsDefaultValueToIntArgument(self):
        '''Calls function with an enum item as default value to an int argument.'''
        self.assertEqual(SampleNamespace.enumItemAsDefaultValueToIntArgument(),
                         SampleNamespace.InValue.ZeroIn)
        self.assertEqual(SampleNamespace.enumItemAsDefaultValueToIntArgument(SampleNamespace.OutValue.ZeroOut),  # noqa E:501
                         SampleNamespace.OutValue.ZeroOut)
        self.assertEqual(SampleNamespace.enumItemAsDefaultValueToIntArgument(123), 123)

    def testAnonymousGlobalEnums(self):
        '''Checks availability of anonymous global enum items.'''
        self.assertEqual(sample.AnonymousGlobalEnum_Value0, 0)
        self.assertEqual(sample.AnonymousGlobalEnum_Value1, 1)

    def testAnonymousClassEnums(self):
        '''Checks availability of anonymous class enum items.'''
        self.assertEqual(SampleNamespace.AnonymousClassEnum_Value0, 0)
        self.assertEqual(SampleNamespace.AnonymousClassEnum_Value1, 1)

    def testNegativeFlagValue(self):
        """PYSIDE-3507: As of Python 3.15, the handling of negative flag values was changed
           (see GH-107538/PR132273). As a result, they need to be converted to their unsigned
           counterparts to maintain the desired behavior in C++."""
        v = SampleNamespace.SignedFlag.MinusOne
        expected = 0xFFFFFFFF
        self.assertEqual(v.value, expected)

        p = SampleNamespace.passThroughSignedFlag(v)
        self.assertEqual(p.value, expected)

    def testEnumClasses(self):
        # C++ 11: values of enum classes need to be fully qualified to match C++
        sum = Event.EventTypeClass.Value1 + Event.EventTypeClass.Value2
        self.assertEqual(sum, 1)

    def testSetEnum(self):
        event = Event(Event.EventType.ANY_EVENT)
        self.assertEqual(event.eventType(), Event.EventType.ANY_EVENT)
        event.setEventType(Event.EventType.BASIC_EVENT)
        self.assertEqual(event.eventType(), Event.EventType.BASIC_EVENT)
        event.setEventTypeByConstRef(Event.EventType.SOME_EVENT)
        self.assertEqual(event.eventType(), Event.EventType.SOME_EVENT)
        event.setEventTypeByConstPtr(Event.EventType.BASIC_EVENT)
        self.assertEqual(event.eventType(), Event.EventType.BASIC_EVENT)

    def testEnumArgumentWithDefaultValue(self):
        '''Option enumArgumentWithDefaultValue(Option opt = UnixTime);'''
        self.assertEqual(SampleNamespace.enumArgumentWithDefaultValue(),
                         SampleNamespace.Option.UnixTime)
        self.assertEqual(SampleNamespace.enumArgumentWithDefaultValue(SampleNamespace.Option.RandomNumber),  # noqa E:501
                         SampleNamespace.Option.RandomNumber)


class MyEvent(Event):
    def __init__(self):
        Event.__init__(self, Event.EventType(3))


class OutOfBoundsTest(unittest.TestCase):
    def testValue(self):
        e = MyEvent()
        self.assertEqual(repr(e.eventType()), "<EventType.ANY_EVENT: 3>")


class EnumOverloadTest(unittest.TestCase):
    '''Test case for overloads involving enums'''

    def testWithInt(self):
        '''Overload with Enums and ints with default value'''
        o = ObjectType()

        self.assertEqual(o.callWithEnum('', Event.EventType.ANY_EVENT, 9), 81)
        self.assertEqual(o.callWithEnum('', 9), 9)


class EnumOperators(unittest.TestCase):
    '''Test case for operations on enums'''

    def testInequalitySameObject(self):
        self.assertFalse(Event.EventType.ANY_EVENT
                         != Event.EventType.ANY_EVENT)


if __name__ == '__main__':
    unittest.main()
