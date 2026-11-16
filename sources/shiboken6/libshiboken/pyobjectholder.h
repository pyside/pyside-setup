// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYOBJECTHOLDER_H
#define PYOBJECTHOLDER_H

#include "gilstate.h"
#include "sbkpython.h"

#include <cassert>
#include <utility>

namespace Shiboken
{

/*!
    \class Shiboken::PyObjectHolder

    \brief Keeps a reference to a PyObject.

    PyObjectHolder holds a PyObject pointer, keeping a reference decrementing
    its reference counter when destroyed. It makes sure to hold the GIL when
    releasing and when copying. If the interpreter has already been finalized,
    which can happen for a holder stored in a global C++ object, the
    reference is leaked instead. It implements copy/move semantics and is mainly intended as a
    base class for functors holding a callable which can be passed around and
    stored in containers or moved from freely.
    For one-shot functors, release() can be invoked after the call.

    \inmodule libshiboken
*/
class PyObjectHolder
{
public:
    PyObjectHolder() noexcept = default;

    /*!
        Constructs a PyObjectHolder holding a new reference to the Python
        object \a pyObj.
    */
    explicit PyObjectHolder(PyObject *pyObj) noexcept : m_pyObj(pyObj)
    {
        assert(pyObj != nullptr);
        Py_INCREF(m_pyObj);
    }

    PyObjectHolder(const PyObjectHolder &o) noexcept : m_pyObj(o.m_pyObj)
    {
        incRef();
    }

    PyObjectHolder &operator=(const PyObjectHolder &o) noexcept
    {
        if (this != &o) {
            m_pyObj = o.m_pyObj;
            incRef();
        }
        return *this;
    }

    PyObjectHolder(PyObjectHolder &&o) noexcept : m_pyObj{std::exchange(o.m_pyObj, nullptr)} {}

    PyObjectHolder &operator=(PyObjectHolder &&o) noexcept
    {
        m_pyObj = std::exchange(o.m_pyObj, nullptr);
        return *this;
    }

    /*!
        Decrements the reference count of the held Python object, unless
        the interpreter is already gone.
    */
    ~PyObjectHolder() { release(); }

    [[nodiscard]] bool isNull() const { return m_pyObj == nullptr; }
    [[nodiscard]] operator bool() const { return m_pyObj != nullptr; }

    /*!
        Returns the pointer of the Python object being held.
    */
    [[nodiscard]] PyObject *object() const { return m_pyObj; }
    [[nodiscard]] operator PyObject *() const { return m_pyObj; }

    [[nodiscard]] PyObject *operator->() { return m_pyObj; }

protected:
    // Copies can be made by C++ code (for example, Qt copying a
    // std::function) on a thread that does not hold the GIL.
    void incRef() const
    {
        if (m_pyObj != nullptr && Py_IsInitialized()) {
            GilState gilState;
            Py_INCREF(m_pyObj);
        }
    }

    void release()
    {
        if (m_pyObj != nullptr) {
            // A holder kept in a global C++ object is destroyed at process
            // exit, after Py_Finalize(). There is no GIL to take then, so
            // the reference is leaked.
            if (Py_IsInitialized()) {
                GilState gilState;
                Py_DECREF(m_pyObj);
            }
            m_pyObj = nullptr;
        }
    }

private:
    PyObject *m_pyObj = nullptr;
};

} // namespace Shiboken

#endif // PYOBJECTHOLDER_H
