// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBKPYRANGESPOLICIES_H
#define SBKPYRANGESPOLICIES_H

#include "autodecref.h"
#include "shibokenclasshelpermacros.h"

#include <cassert>

// Internal
namespace Shiboken::Detail {

// Life time/ownership policies for PyTupleRangeT: Default: raw pointer/non-owning, trivially copyable, zero cost
struct PyRangeDefaultLifetimePolicy
{
    explicit PyRangeDefaultLifetimePolicy(PyObject *) noexcept {}
};

// AutoDecRef-based life time policy (keep a reference).
struct AutoDecRefLifetimePolicy : public Shiboken::AutoDecRef
{
    explicit AutoDecRefLifetimePolicy(PyObject *o) noexcept : Shiboken::AutoDecRef(Py_XNewRef(o)) {}
};

#ifdef Py_GIL_DISABLED
class CriticalSectionLifetimePolicy
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(CriticalSectionLifetimePolicy)

    explicit CriticalSectionLifetimePolicy(PyObject *obj) noexcept
    {
        assert(obj != nullptr);
        PyCriticalSection_Begin(&m_cs, obj);
    }

    ~CriticalSectionLifetimePolicy()
    {
        PyCriticalSection_End(&m_cs);
    }

private:
    PyCriticalSection m_cs;
};
#endif // Py_GIL_DISABLED

} // namespace Shiboken::Detail

#endif // SBKPYRANGESPOLICIES_H
