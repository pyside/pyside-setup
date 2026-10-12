// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_FAILPOINT_H
#define SBK_FAILPOINT_H

#include "sbkpython.h"
#include "shibokenmacros.h"

#ifdef Py_GIL_DISABLED

// Parking needs PyGILState_Check(), which the limited API lacks
#if defined(NDEBUG) || defined(Py_LIMITED_API)
#  define SBK_NO_FAILPOINTS
#endif

namespace Shiboken
{

#ifndef SBK_NO_FAILPOINTS

LIBSHIBOKEN_API bool armFailpoint(const char *name, int timeoutMs = 5000);
LIBSHIBOKEN_API bool releaseFailpoint(const char *name);
LIBSHIBOKEN_API bool failpointReached(const char *name);
LIBSHIBOKEN_API void clearFailpoints();
LIBSHIBOKEN_API const char *failpointNames();
LIBSHIBOKEN_API void failpoint(const char *name);

#else

inline bool armFailpoint(const char *, int = 5000) { return false; }
inline bool releaseFailpoint(const char *) { return false; }
inline bool failpointReached(const char *) { return false; }
inline void clearFailpoints() {}
inline const char *failpointNames() { return ""; }
inline void failpoint(const char *) {}

#endif // SBK_NO_FAILPOINTS

} // namespace Shiboken

#endif // Py_GIL_DISABLED

#endif // SBK_FAILPOINT_H
