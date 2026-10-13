// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBKTYPETUPLEREF_H
#define SBKTYPETUPLEREF_H

#include "sbkpython.h"
#include "sbkpytuplerange.h"
#include "shibokenclasshelpermacros.h"
#include "shibokenmacros.h"

namespace Shiboken::Detail {

#ifdef Py_GIL_DISABLED
enum class PepTypeTuple { Mro, Bases };

// Holds the tuple before PepTypeTupleRef builds its range over it.
class LIBSHIBOKEN_API PepTypeTupleOwner
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(PepTypeTupleOwner)

protected:
    PepTypeTupleOwner(PyTypeObject *type, PepTypeTuple which);
    ~PepTypeTupleOwner();

    PyObject *m_typeTuple = nullptr;
    bool m_owned = false;
};
#else // Py_GIL_DISABLED
// The GIL build walks the borrowed tuple: nothing can free it during the walk.
class PepTypeTupleOwner
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(PepTypeTupleOwner)

protected:
    explicit PepTypeTupleOwner(PyObject *tuple) noexcept : m_typeTuple(tuple) {}
    ~PepTypeTupleOwner() = default;

    PyObject *m_typeTuple;
};
#endif // Py_GIL_DISABLED

} // namespace Shiboken::Detail

/*!
    \class PepTypeTupleRef
    \ingroup freethreading
    \inmodule libshiboken

    A Shiboken::PyTupleTypeRange over a type's \c __mro__ or \c __bases__
    for one walk. A GIL build borrows the tuple; a free-threaded build takes
    an owned snapshot through type's own getter (no raw lock may be held: it
    takes the type lock), since an assignment to \c __bases__ in another
    thread frees the old tuples. A metaclass that shadows \c __mro__ or
    \c __bases__ does not change the tuple. Without the MroSnapshot bit, the
    snapshot's reference is dropped again while the tuple is still the
    type's, so the walk borrows it, as the base does. The range is empty for
    a type without the tuple, as with the GIL, and an exception the caller
    has set is kept across the snapshot.

    Use a named PepMroRef or PepBasesRef only: the range of a temporary's
    chop() or slice() would outlive it. object() is the tuple.
*/
class PepTypeTupleRef : private Shiboken::Detail::PepTypeTupleOwner,
                        public Shiboken::PyTupleTypeRange
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(PepTypeTupleRef)

    using Shiboken::PyTupleTypeRange::chop;
    using Shiboken::PyTupleTypeRange::slice;
    void chop(Py_ssize_t) && = delete;
    void slice(Py_ssize_t) && = delete;

    PyObject *object() const { return m_typeTuple; }

protected:
#ifdef Py_GIL_DISABLED
    PepTypeTupleRef(PyTypeObject *type, Shiboken::Detail::PepTypeTuple which)
        : PepTypeTupleOwner(type, which), Shiboken::PyTupleTypeRange(m_typeTuple) {}
#else // Py_GIL_DISABLED
    explicit PepTypeTupleRef(PyObject *tuple) noexcept
        : PepTypeTupleOwner(tuple), Shiboken::PyTupleTypeRange(tuple) {}
#endif // Py_GIL_DISABLED
    ~PepTypeTupleRef() = default;
};

/*!
    \class PepMroRef
    \ingroup freethreading
    \inmodule libshiboken

    A PepTypeTupleRef of a type's \c __mro__.
*/
class PepMroRef : public PepTypeTupleRef
{
public:
#ifdef Py_GIL_DISABLED
    explicit PepMroRef(PyTypeObject *type)
        : PepTypeTupleRef(type, Shiboken::Detail::PepTypeTuple::Mro) {}
#else // Py_GIL_DISABLED
    explicit PepMroRef(PyTypeObject *type) noexcept : PepTypeTupleRef(type->tp_mro) {}
#endif // Py_GIL_DISABLED
};

/*!
    \class PepBasesRef
    \ingroup freethreading
    \inmodule libshiboken

    A PepTypeTupleRef of a type's \c __bases__.
*/
class PepBasesRef : public PepTypeTupleRef
{
public:
#ifdef Py_GIL_DISABLED
    explicit PepBasesRef(PyTypeObject *type)
        : PepTypeTupleRef(type, Shiboken::Detail::PepTypeTuple::Bases) {}
#else // Py_GIL_DISABLED
    explicit PepBasesRef(PyTypeObject *type) noexcept : PepTypeTupleRef(type->tp_bases) {}
#endif // Py_GIL_DISABLED
};

#endif // SBKTYPETUPLEREF_H
