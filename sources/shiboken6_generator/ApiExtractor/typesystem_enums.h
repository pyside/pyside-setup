// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#ifndef TYPESYSTEM_ENUMS_H
#define TYPESYSTEM_ENUMS_H

#include <cstdint>

namespace TypeSystem
{
enum Language : std::uint8_t {
    TargetLangCode      = 0x0001,
    NativeCode          = 0x0002,
    ShellCode           = 0x0004,

    // masks
    All                 = TargetLangCode | NativeCode | ShellCode,

    TargetLangAndNativeCode   = TargetLangCode | NativeCode
};

enum class AllowThread : std::uint8_t {
    Unspecified,
    Allow,
    Disallow,
    Auto
};

enum Ownership : std::uint8_t {
    UnspecifiedOwnership,
    DefaultOwnership,
    TargetLangOwnership,
    CppOwnership
};

enum CodeSnipPosition : std::uint8_t {
    CodeSnipPositionBeginning,
    CodeSnipPositionEnd,
    CodeSnipPositionDeclaration,
    CodeSnipPositionPyOverride,
    CodeSnipPositionWrapperDeclaration,
    CodeSnipPositionAny
};

enum class DeletionMode : std::uint8_t {
    Default,
    DeleteInMainThread, // libshiboken
    DeleteInQObjectOwnerThread, // libpyside for QObjects
};

enum DocModificationMode : std::uint8_t {
    DocModificationAppend,
    DocModificationPrepend,
    DocModificationReplace,
    DocModificationXPathReplace
};

enum class DocMode : std::uint8_t {
    Nested,
    Flat
};

enum class ExceptionHandling : std::uint8_t {
    Unspecified,
    Off,
    AutoDefaultToOff,
    AutoDefaultToOn,
    On
};

enum class SnakeCase : std::uint8_t {
    Unspecified,
    Disabled,
    Enabled,
    Both
};

enum Visibility : std::uint8_t { // For namespaces
    Unspecified,
    Visible,
    Invisible,
    Auto
};

enum class BoolCast : std::uint8_t { // Generate nb_bool (overriding command line)
    Unspecified,
    Disabled,
    Enabled
};

enum class ModuleGil : std::uint8_t { // Py_mod_gil in a free-threaded build
    Used,
    NotUsed,
    Auto // NotUsed if Py_GIL_DISABLED is defined, else Used
};

enum class CPythonType : std::uint8_t
{
    Bool,
    Float,
    Integer,
    String,
    Other
};

enum class QtMetaTypeRegistration : std::uint8_t
{
    Unspecified,
    Enabled,
    BaseEnabled, // Registration only for the base class of a hierarchy
    Disabled
};

enum class SmartPointerType : std::uint8_t {
    Shared,
    Unique,
    Handle,
    ValueHandle
};

enum class SmartPointerToPythonConversion : std::uint8_t {
    Default,
    NullAsNone,
};

enum class PythonEnumType : std::uint8_t {
    Unspecified,
    Enum,
    IntEnum,
    Flag,
    IntFlag
};

enum class DefaultConstructibleFlag : unsigned char {
    Unspecified,
    Enabled,
    Disabled
};

enum class CopyableFlag : unsigned char {
    Unspecified,
    Enabled,
    Disabled
};

enum class MovableFlag : unsigned char {
    Unspecified,
    Enabled,
    Disabled
};

enum class DestructorType : unsigned char {
    NoDestructor,
    ProtectedDestructor,
    PublicDestructor
};

enum class DestructibleFlag : unsigned char {
    Unspecified,
    Enabled,
    Disabled
};

enum : int { OverloadNumberUnset = -1, OverloadNumberDefault = 99999 };

} // namespace TypeSystem

#endif // TYPESYSTEM_ENUMS_H
