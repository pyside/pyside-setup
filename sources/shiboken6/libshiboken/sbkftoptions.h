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
};

LIBSHIBOKEN_API bool optionEnabled(Option opt);
LIBSHIBOKEN_API std::uint64_t optionFlags();

} // namespace Shiboken::FreeThreading

#endif // Py_GIL_DISABLED

#endif // SBK_FTOPTIONS_H
