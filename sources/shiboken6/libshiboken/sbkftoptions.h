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
};

LIBSHIBOKEN_API bool optionEnabled(Option opt);
LIBSHIBOKEN_API std::uint64_t optionFlags();

} // namespace Shiboken::FreeThreading

#endif // Py_GIL_DISABLED

#endif // SBK_FTOPTIONS_H
