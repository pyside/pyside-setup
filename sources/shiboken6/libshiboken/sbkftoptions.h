// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_FTOPTIONS_H
#define SBK_FTOPTIONS_H

#include "sbkpython.h"
#include "shibokenmacros.h"

#ifdef Py_GIL_DISABLED

#include <cstdint>

namespace Shiboken::FreeThreading {

enum class Option : std::uint64_t
{
    OwnerThreadDeletion = 0x1,
    FinishedOwnerCleanup = 0x2,
    StateLock = 0x4,
    DictPublishOnce = 0x8,
    OwnedWrapperLookup = 0x10,
    OneWrapperPerAddress = 0x20,
    ConstructorInstallOnce = 0x40,
    ClassAssignCheck = 0x80,
    InvalidateRootsOwned = 0x100,
    ContainerArgumentCopy = 0x200,
    ClassHierarchySnapshot = 0x400,
    MiOffsetsOnce = 0x800,
    DocRecursionPerThread = 0x1000,
    PostRoutineBatch = 0x2000,
    DictIterationSnapshot = 0x4000,
    QObjectMetaTypeRecords = 0x8000,
    MroSnapshot = 0x10000,
    ConverterRegistryLock = 0x20000,
    ModuleRegistryLock = 0x40000,
    ModuleImportWaits = 0x80000,
    FirstUseOnce = 0x100000,
    ModuleDirDefault = 0x200000,
    EnumPrivateLock = 0x400000,
    QEnumCollectorPerThread = 0x800000,
    GenericEnumRegistryLock = 0x1000000,
    StreamHelperOnce = 0x2000000,
    WrapperWalkOwned = 0x4000000,
    WrapperDumpLocked = 0x8000000,
    MainThreadQueueLock = 0x10000000,
    ModuleDictKept = 0x20000000,
    StaticInitDetached = 0x40000000000000,
    NewTypeQualname = 0x80000000000000,
};

LIBSHIBOKEN_API bool optionEnabled(Option opt);
LIBSHIBOKEN_API std::uint64_t optionFlags();

} // namespace Shiboken::FreeThreading

#endif // Py_GIL_DISABLED

#endif // SBK_FTOPTIONS_H
