// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBKPYSEQUENCERANGE_H
#define SBKPYSEQUENCERANGE_H

#include "autodecref.h"
#include "sbkpyrangespolicies.h"
#include "sbkpython.h"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace Shiboken {

/*!
\class Shiboken::PySequenceIterator

\brief Iterator for \e{CPython} sequences returning an AutoDecRef.

\inmodule libshiboken
*/
class PySequenceIterator
{
public:
    // Note: Strictly, LegacyBidirectionalIterator requires a reference type,
    // but std::prev()/std::advance() dispatch on the category in C++17 and
    // would loop endlessly for negative distances with input_iterator_tag.
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;

    using value_type = AutoDecRef;
    using pointer = void; // No meaningful operator ->
    using reference = value_type; // Values are computed, cannot obtain a reference
    using difference_type = Py_ssize_t;

    explicit PySequenceIterator(PyObject *sequence, Py_ssize_t pos) noexcept :
        m_sequence(sequence), m_pos(pos) {}

    PySequenceIterator() noexcept = default;

    // Returns a new reference which is null with an exception set on failure
    // (for example, IndexError when the sequence shrinks during iteration).
    value_type operator*() const { return value_type(PySequence_GetItem(m_sequence, m_pos)); }

    PySequenceIterator &operator++() { ++m_pos; return *this; }
    PySequenceIterator &operator--() { --m_pos; return *this; }

    PySequenceIterator operator++(int)
    {
        PySequenceIterator result = *this;
        ++m_pos;
        return result;
    }
    PySequenceIterator operator--(int)
    {
        PySequenceIterator result = *this;
        --m_pos;
        return result;
    }

    bool equals(PySequenceIterator rhs) const
    {
        assert(m_sequence == rhs.m_sequence);
        return m_pos == rhs.m_pos;
    }

private:
    friend bool operator==(PySequenceIterator i1, PySequenceIterator i2)
    { return i1.equals(i2); }

    friend bool operator!=(PySequenceIterator i1, PySequenceIterator i2)
    { return !i1.equals(i2); }

    PyObject *m_sequence = nullptr;
    Py_ssize_t m_pos = 0;
};

namespace Detail {

// Base template for ranges of the of above sequence iterator types.
template <class LifetimePolicy = PyRangeDefaultLifetimePolicy>
class PySequenceRangeT : private LifetimePolicy
{
public:
    using iterator = PySequenceIterator;

    explicit PySequenceRangeT(PyObject *sequence) noexcept :
        LifetimePolicy(sequence), m_sequence(sequence),
        m_end(sequence != nullptr ? std::max(PySequence_Size(sequence), Py_ssize_t(0)) : 0)
    {
        assert(sequence == nullptr || PySequence_Check(sequence) != 0);
    }

    iterator begin() const { return iterator(m_sequence, m_begin); }
    iterator end() const { return iterator(m_sequence, m_end); }

    Py_ssize_t size() const { return m_end - m_begin; }
    bool isEmpty() const { return m_begin >= m_end; }

private:
    PyObject *m_sequence = nullptr;
    Py_ssize_t m_begin = 0;
    Py_ssize_t m_end = 0;
};

} // Detail

/*!
\typedef Shiboken::PySequenceRange

\brief Range for \e{CPython} sequences using the sequence protocol.

Dereferencing the iterator returns an AutoDecRef holding the new reference
returned by \c{PySequence_GetItem()}, which is released at the end of the
loop iteration. The item may be null with an exception set (for example,
when the sequence shrinks during iteration):

\code
for (const auto &item : Shiboken::PySequenceRange(seq)) {
    if (item.isNull())
        return false;
    ...
}
\endcode

The size is determined once on construction. If \c{PySequence_Size()} fails
(infinite sequence), the range is empty and the Python error is left set.

\note Do \e{not} declare the loop variable as \c{PyObject *}.
      It will compile, but potentially cause dangling pointers and crashes.

\inmodule libshiboken
*/
using PySequenceRange = Detail::PySequenceRangeT<Detail::PyRangeDefaultLifetimePolicy>;

} // namespace Shiboken

#endif // SBKPYSEQUENCERANGE_H
