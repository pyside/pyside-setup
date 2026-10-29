// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBKPYTUPLERANGE_H
#define SBKPYTUPLERANGE_H

#include "sbkpyrangespolicies.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace Shiboken {

/*!
    \namespace Shiboken::Detail
    \brief Internal namespace
    \internal
    \inmodule libshiboken
*/
namespace Detail {

// Policies for the value returned by PyTupleIteratorT::operator*()
struct PyTupleItemIteratorPolicy
{
    using value_type = PyObject *;

    static value_type get(PyObject *tuple, Py_ssize_t pos)
    { return PyTuple_GetItem(tuple, pos); }
};

struct PyTupleTypeIteratorPolicy
{
    using value_type = PyTypeObject *;

    static value_type get(PyObject *tuple, Py_ssize_t pos)
    {
        PyObject *item = PyTuple_GetItem(tuple, pos);
        assert(PyType_Check(item) != 0);
        return reinterpret_cast<PyTypeObject *>(item);
    }
};

struct PyTupleEnumeratedItemIteratorPolicy
{
    using value_type = std::pair<Py_ssize_t, PyObject *>;

    static value_type get(PyObject *tuple, Py_ssize_t pos)
    { return {pos, PyTuple_GetItem(tuple, pos)}; }
};

struct PyTupleEnumeratedTypeIteratorPolicy
{
    using value_type = std::pair<Py_ssize_t, PyTypeObject *>;

    static value_type get(PyObject *tuple, Py_ssize_t pos)
    {
        PyObject *item = PyTuple_GetItem(tuple, pos);
        assert(PyType_Check(item) != 0);
        return {pos, reinterpret_cast<PyTypeObject *>(item)};
    }
};

// Iterator for Python tuples whose value type can be configured
// by the ValuePolicy.
template <class ValuePolicy>
class PyTupleIteratorT
{
public:
    // Note: Strictly, LegacyBidirectionalIterator requires a reference type,
    // but std::prev()/std::advance() dispatch on the category in C++17 and
    // would loop endlessly for negative distances with input_iterator_tag.
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;

    using value_type = typename ValuePolicy::value_type;
    using pointer = void; // No meaningful operator ->
    using reference = value_type; // Values are computed, cannot obtain a reference
    using difference_type = Py_ssize_t;

    explicit PyTupleIteratorT(PyObject *tuple, Py_ssize_t pos) noexcept :
        m_tuple(tuple), m_pos(pos) {}

    PyTupleIteratorT() noexcept = default;

    value_type operator*() const { return ValuePolicy::get(m_tuple, m_pos); }

    PyTupleIteratorT &operator++() { ++m_pos; return *this; }
    PyTupleIteratorT &operator--() { --m_pos; return *this; }

    PyTupleIteratorT operator++(int)
    {
        PyTupleIteratorT result = *this;
        ++m_pos;
        return result;
    }
    PyTupleIteratorT operator--(int)
    {
        PyTupleIteratorT result = *this;
        --m_pos;
        return result;
    }

    bool equals(PyTupleIteratorT rhs) const
    {
        assert(m_tuple == rhs.m_tuple);
        return m_pos == rhs.m_pos;
    }

private:
    friend bool operator==(PyTupleIteratorT i1, PyTupleIteratorT i2)
    { return i1.equals(i2); }

    friend bool operator!=(PyTupleIteratorT i1, PyTupleIteratorT i2)
    { return !i1.equals(i2); }

    PyObject *m_tuple = nullptr;
    Py_ssize_t m_pos = 0;
};

// Base template for ranges of the above tuple iterator types.
template <class Iterator, class LifetimePolicy = PyRangeDefaultLifetimePolicy>
class PyTupleRangeT : private LifetimePolicy
{
public:
    explicit PyTupleRangeT(PyObject *tuple) noexcept :
        LifetimePolicy(tuple),
        m_tuple(tuple),
        m_end(tuple != nullptr && PyTuple_Check(tuple) != 0 ? PyTuple_Size(tuple) : 0)
    {
        assert(tuple == nullptr || PyTuple_Check(tuple) != 0);
    }

    Iterator begin() const { return Iterator(m_tuple, m_begin); }
    Iterator end() const { return Iterator(m_tuple, m_end); }

    Py_ssize_t size() const { return m_end - m_begin; }
    bool isEmpty() const { return m_begin >= m_end; }

    // Ref-qualified overloads: Modify named ranges in-place and return a
    // reference; move temporaries out (for example, in range-based for loops)
    // to avoid returning a dangling reference.
    PyTupleRangeT &chop(Py_ssize_t n) &
    {
        doChop(n);
        return *this;
    }

    [[nodiscard]] PyTupleRangeT chop(Py_ssize_t n) &&
    {
        doChop(n);
        return std::move(*this);
    }

    PyTupleRangeT &slice(Py_ssize_t n) &
    {
        doSlice(n);
        return *this;
    }

    [[nodiscard]] PyTupleRangeT slice(Py_ssize_t n) &&
    {
        doSlice(n);
        return std::move(*this);
    }

private:
    void doChop(Py_ssize_t n)
    {
        assert(n >= 0);
        m_end = std::max(m_begin, m_end - n);
    }

    void doSlice(Py_ssize_t n)
    {
        assert(n >= 0);
        m_begin = std::min(m_end, m_begin + n);
    }

    PyObject *m_tuple = nullptr;
    Py_ssize_t m_begin = 0;
    Py_ssize_t m_end = 0;
};

} // namespace Detail

/*!
\typedef Shiboken::PyTupleIterator

\brief Iterator for \e{CPython} tuples returning the tuple items.

\inmodule libshiboken
*/
using PyTupleIterator = Detail::PyTupleIteratorT<Detail::PyTupleItemIteratorPolicy>;

/*!
\typedef Shiboken::PyTupleTypeIterator

\brief Iterator for \e{CPython} tuples returning the tuple items as a \c{PyTypeObject *}.

\sa PyTupleIterator
\inmodule libshiboken
*/
using PyTupleTypeIterator = Detail::PyTupleIteratorT<Detail::PyTupleTypeIteratorPolicy>;

/*!
\typedef Shiboken::PyTupleEnumeratedIterator

\brief Iterator for \e{CPython} tuples returning pairs of \c{(index, item)}.

\sa PyTupleIterator
\inmodule libshiboken
*/
using PyTupleEnumeratedIterator =
    Detail::PyTupleIteratorT<Detail::PyTupleEnumeratedItemIteratorPolicy>;

/*!
\typedef Shiboken::PyTupleEnumeratedTypeIterator

\brief Iterator for \e{CPython} tuples returning pairs of \c{(index, type)}.

\sa PyTupleTypeIterator
\inmodule libshiboken
*/
using PyTupleEnumeratedTypeIterator =
    Detail::PyTupleIteratorT<Detail::PyTupleEnumeratedTypeIteratorPolicy>;

/*!
\typedef Shiboken::PyTupleRange

\brief Range for \e{CPython} tuples.

\sa PyTupleIterator
\inmodule libshiboken
*/
using PyTupleRange = Detail::PyTupleRangeT<PyTupleIterator>;

/*!
\typedef Shiboken::PyTupleTypeRange

\brief Range for \e{CPython} tuples returning a \c{PyTypeObject *}.

\sa PyTupleTypeIterator
\inmodule libshiboken
*/
using PyTupleTypeRange = Detail::PyTupleRangeT<PyTupleTypeIterator>;

/*!
\typedef Shiboken::OwningPyTupleTypeRange

\brief Range for \e{CPython} tuples returning a \c{PyTypeObject *} that keeps a reference on
       the tuple.

\sa PyTupleTypeIterator
\inmodule libshiboken
*/
using OwningPyTupleTypeRange = Detail::PyTupleRangeT<PyTupleTypeIterator, Detail::AutoDecRefLifetimePolicy>;

/*!
\typedef Shiboken::PyTupleEnumeratedRange

\brief Range for \e{CPython} tuples returning pairs of \c{(index, item)}.

The index is the absolute position within the tuple. Use with
structured bindings as in:

\code
for (auto [idx, item] : Shiboken::PyTupleEnumeratedRange(tuple))
    ...
\endcode

\sa PyTupleEnumeratedIterator
\inmodule libshiboken
*/
using PyTupleEnumeratedRange = Detail::PyTupleRangeT<PyTupleEnumeratedIterator>;

/*!
\typedef Shiboken::PyTupleEnumeratedTypeRange

\brief Range for \e{CPython} tuples returning pairs of \c{(index, type)}.

\sa PyTupleEnumeratedTypeIterator
\inmodule libshiboken
*/
using PyTupleEnumeratedTypeRange = Detail::PyTupleRangeT<PyTupleEnumeratedTypeIterator>;

} // namespace Shiboken

#endif // SBKPYTUPLERANGE_H
