// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "samplenamespace.h"
#include "moveonly.h"

#include <iostream>
#include <cstdlib>
#include <ctime>

namespace SampleNamespace
{

SomeClass::OptionAlias SomeClass::passThroughOptionAlias(OptionAlias ov)
{
    return ov;
}

Option SomeClass::passThroughOption(Option ov)
{
    return ov;
}

// PYSIDE-817, scoped enums must not be converted to int in the wrappers generated
// for the protected hacks
SomeClass::PublicScopedEnum SomeClass::protectedMethodReturningPublicScopedEnum() const
{
    return PublicScopedEnum::v1;
}

OutValue enumInEnumOut(InValue in)
{
    auto retval = OutValue(-1);
    switch(in) {
    case ZeroIn:
        retval = ZeroOut;
        break;
    case OneIn:
        retval = OneOut;
        break;
    case TwoIn:
        retval = TwoOut;
        break;
    default:
        break;
    }
    return retval;
}

Option enumArgumentWithDefaultValue(Option opt)
{
    return opt;
}

SignedFlag passThroughSignedFlag(SignedFlag in)
{
    return in;
}

int getNumber(Option opt)
{
    int retval;
    switch(opt) {
    case RandomNumber:
        retval = rand() % 100;
        break;
    case UnixTime:
        retval = int(std::time(nullptr));
        break;
    default:
        retval = 0;
        break;
    }
    return retval;
}

void doSomethingWithArray(const unsigned char *, unsigned int, const char *)
{
    // This function does nothing in fact.
    // It is here as a dummy copy of QPixmap.loadFromData method
    // to check compilation issues, i.e. if it compiles, it's ok.
}

int enumItemAsDefaultValueToIntArgument(int value)
{
    return value;
}

void forceDecisorSideA(ObjectType *)
{
}

void forceDecisorSideA(const Point &, const Str &, ObjectType *)
{
}

void forceDecisorSideB(int, ObjectType *)
{
}

void forceDecisorSideB(int, const Point &, const Str &, ObjectType *)
{
}

double passReferenceToValueType(const Point &point, double multiplier)
{
    return (point.x() + point.y()) * multiplier;
}

int passReferenceToObjectType(const ObjectType &obj, int multiplier)
{
    return obj.objectName().size() * multiplier;
}

// Exercise specifying complete template specializations as primitive types.
std::optional<long> optionalMultiply(const std::optional<long> &v1,
                                     const std::optional<long> &v2)
{
    if (!v1.has_value() || !v2.has_value())
        return std::nullopt;
    return v1.value() * v2.value();
}

int variableInNamespace = 42;

} // namespace SampleNamespace
