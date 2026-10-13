// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_WRAPPERREF_H
#define SBK_WRAPPERREF_H

#include "sbkpython.h"
#include "shibokenclasshelpermacros.h"

#include <utility>

struct SbkObject;

namespace Shiboken
{

class BindingManager;

// A wrapper from the binding map. Free-threaded builds hold a reference,
// since another thread can drop the last one; GIL builds borrow it.
class WrapperRef
{
public:
    WrapperRef() noexcept = default;
    LIBSHIBOKEN_DISABLE_COPY(WrapperRef)

    WrapperRef(WrapperRef &&o) noexcept : m_obj(std::exchange(o.m_obj, nullptr)) {}
    WrapperRef &operator=(WrapperRef &&o) noexcept
    {
        if (this != &o) {
            reset();
            m_obj = std::exchange(o.m_obj, nullptr);
        }
        return *this;
    }

    ~WrapperRef() { reset(); }

    // Deleted on temporaries: the pointer must not outlive the reference
    [[nodiscard]] SbkObject *object() const & noexcept { return m_obj; }
    SbkObject *object() const && = delete;
    [[nodiscard]] PyObject *pyObject() const & noexcept
    { return reinterpret_cast<PyObject *>(m_obj); }
    PyObject *pyObject() const && = delete;
    [[nodiscard]] bool isNull() const noexcept { return m_obj == nullptr; }

    explicit operator bool() const noexcept { return m_obj != nullptr; }

    // A new reference to the wrapper, for the caller; this one is empty afterwards
    [[nodiscard]] PyObject *newRef() noexcept
    {
#ifdef Py_GIL_DISABLED
        return reinterpret_cast<PyObject *>(std::exchange(m_obj, nullptr));
#else
        auto *result = reinterpret_cast<PyObject *>(std::exchange(m_obj, nullptr));
        Py_XINCREF(result);
        return result;
#endif
    }

#ifdef Py_GIL_DISABLED
    [[nodiscard]] SbkObject *release() noexcept { return std::exchange(m_obj, nullptr); }

    void reset() noexcept
    {
        Py_XDECREF(reinterpret_cast<PyObject *>(std::exchange(m_obj, nullptr)));
    }
#else
    void reset() noexcept { m_obj = nullptr; }
#endif

private:
    friend class BindingManager;
#ifdef Py_GIL_DISABLED
    static WrapperRef fromOwned(SbkObject *obj) noexcept { return WrapperRef(obj); }
#else
    static WrapperRef fromBorrowed(SbkObject *obj) noexcept { return WrapperRef(obj); }
#endif

    explicit WrapperRef(SbkObject *obj) noexcept : m_obj(obj) {}

    SbkObject *m_obj = nullptr;
};

} // namespace Shiboken

#endif // SBK_WRAPPERREF_H
