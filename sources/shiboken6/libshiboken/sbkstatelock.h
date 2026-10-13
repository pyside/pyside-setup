// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_STATELOCK_H
#define SBK_STATELOCK_H

#include "sbkpython.h"

#ifdef Py_GIL_DISABLED

#include "shibokenmacros.h"
#include "shibokenclasshelpermacros.h"

#include <vector>

struct SbkObject;

namespace Shiboken {

LIBSHIBOKEN_API void stateLockAcquire();
LIBSHIBOKEN_API void stateLockRelease();
LIBSHIBOKEN_API bool stateLockHeldByCurrentThread();

class StateLockGuard
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(StateLockGuard)

    StateLockGuard() { stateLockAcquire(); }
    ~StateLockGuard() { stateLockRelease(); }
};

class LIBSHIBOKEN_API DeferredDecrefs
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(DeferredDecrefs)

    DeferredDecrefs() noexcept = default;
    ~DeferredDecrefs();

    void addDecref(PyObject *o);
    void addDecref(SbkObject *o) { addDecref(reinterpret_cast<PyObject *>(o)); }

    void run();

private:
    std::vector<PyObject *> m_objects;
};

} // namespace Shiboken

#endif // Py_GIL_DISABLED

#endif // SBK_STATELOCK_H
