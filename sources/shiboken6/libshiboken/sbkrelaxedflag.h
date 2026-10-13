// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_RELAXEDFLAG_H
#define SBK_RELAXEDFLAG_H

#include "sbkpython.h"

#ifdef Py_GIL_DISABLED

#include "shibokenclasshelpermacros.h"

#include <atomic>

namespace Shiboken {

class RelaxedFlag
{
public:
    RelaxedFlag() noexcept = default;
    LIBSHIBOKEN_DISABLE_COPY_MOVE(RelaxedFlag)
    ~RelaxedFlag() = default;

    RelaxedFlag &operator=(bool value) noexcept
    {
        m_value.store(value, std::memory_order_relaxed);
        return *this;
    }

    operator bool() const noexcept
    {
        return m_value.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_value;
};

static_assert(std::atomic<bool>::is_always_lock_free,
              "RelaxedFlag must not compile to a mutex");
static_assert(sizeof(RelaxedFlag) == 1 && alignof(RelaxedFlag) == 1,
              "RelaxedFlag must cost no more than the bool it replaces");

} // namespace Shiboken

#endif // Py_GIL_DISABLED

#endif // SBK_RELAXEDFLAG_H
