// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBKPYDICTRANGE_H
#define SBKPYDICTRANGE_H

#include "sbkpyrangespolicies.h"

#include <cassert>
#include <iterator>
#include <utility>

namespace Shiboken {

using PyObjectPair = std::pair<PyObject *, PyObject *>;

/*!
\class Shiboken::PyDictIterator

\brief Iterator for \e{CPython} dicts returning pairs of \c{(key, value)}.

\inmodule libshiboken
*/
class PyDictIterator
{
public:
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    using value_type = PyObjectPair;
    using pointer = void; // No meaningful operator ->
    using reference = value_type; // Values are computed, cannot obtain a reference
    using difference_type = Py_ssize_t;

    explicit PyDictIterator(PyObject *dict) noexcept :
        m_dict(dict)
    {
        assert(dict == nullptr || PyDict_Check(dict) != 0);
        if (dict != nullptr)
            next();
    }

    PyDictIterator() noexcept = default;

    value_type operator*() const { return m_value; }
    PyObject *key() const { return m_value.first; } // Qt style
    PyObject *value() const { return m_value.second; }

    PyDictIterator &operator++()
    {
        next();
        return *this;
    }

    PyDictIterator operator++(int)
    {
        PyDictIterator result = *this;
        next();
        return result;
    }

    bool equals(const PyDictIterator &rhs) const
    {
        // End or default constructed sentinel
        if (atEnd() || rhs.atEnd())
            return atEnd() && rhs.atEnd();
        assert(m_dict == rhs.m_dict);
        return m_pos == rhs.m_pos;
    }

private:
    friend bool operator==(const PyDictIterator &i1, const PyDictIterator &i2)
    { return i1.equals(i2); }

    friend bool operator!=(const PyDictIterator &i1, const PyDictIterator &i2)
    { return !i1.equals(i2); }

    void next()
    {
        if (PyDict_Next(m_dict, &m_pos, &m_value.first, &m_value.second) == 0)
            m_value.first = m_value.second = nullptr;
    }

    bool atEnd() const { return m_value.first == nullptr; }

    PyObject *m_dict = nullptr;
    value_type m_value{nullptr, nullptr};
    Py_ssize_t m_pos = 0;
};

namespace Detail {

// Base template for ranges of the above dict iterator types.
template <class LifetimePolicy = PyRangeDefaultLifetimePolicy>
class PyDictRangeT : private LifetimePolicy
{
public:
    explicit PyDictRangeT(PyObject *dict) noexcept : LifetimePolicy(dict), m_dict(dict) {}

    PyDictIterator begin() const { return PyDictIterator(m_dict); }
    PyDictIterator end() const { return {}; }

private:
    PyObject *m_dict;
};

} // namespace Detail

/*!
\typedef Shiboken::PyDictRange

\brief Range for \e{CPython} dicts returning pairs of \c{(key, value)}.

Use with structured bindings as in:

\code
for (auto [key, value] : Shiboken::PyDictRange(dict))
    ...
\endcode

\sa PyDictIterator
\inmodule libshiboken
*/
using PyDictRange = Detail::PyDictRangeT<Detail::PyRangeDefaultLifetimePolicy>;

#ifdef Py_GIL_DISABLED
/*!
\typedef Shiboken::PyDictThreadSafeRange

\brief Range for \e{CPython} dicts returning pairs of \c{(key, value)}.

The range executes within a critical section.
\sa PyDictRange
\inmodule libshiboken
*/
using PyDictThreadSafeRange = Detail::PyDictRangeT<Detail::CriticalSectionLifetimePolicy>;
#endif // Py_GIL_DISABLED

} // namespace Shiboken

#endif // SBKPYDICTRANGE_H
