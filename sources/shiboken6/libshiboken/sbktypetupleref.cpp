// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbktypetupleref.h"

#ifdef Py_GIL_DISABLED

#include "autodecref.h"
#include "pep384impl.h"
#include "sbkfailpoint.h"
#include "sbkftoptions.h"
#include "sbkheldlocks.h"
#include "sbkstaticstrings_p.h"

#include <cassert>

namespace Shiboken::Detail {

// The tuple from type's own getter, which takes the type lock. A metaclass can shadow
// __mro__ and __bases__ with any value; the getter returns the field.
static PyObject *typeTupleFromGetter(PyTypeObject *type, PyObject *name)
{
    AutoDecRef typeDict(PepType_GetDict(&PyType_Type));
    PyObject *descr{};
    if (PyDict_GetItemRef(typeDict.object(), name, &descr) <= 0) {
        if (PyErr_Occurred() == nullptr)
            PyErr_SetObject(PyExc_AttributeError, name);
        return nullptr;
    }
    AutoDecRef keep(descr);
    return Py_TYPE(descr)->tp_descr_get(descr, reinterpret_cast<PyObject *>(type),
                                        reinterpret_cast<PyObject *>(Py_TYPE(type)));
}

// The owned tuple, or nullptr for a type without it.
static PyObject *typeTupleSnapshot(PyTypeObject *type, PyObject **field, bool bases)
{
    // A type without the tuple: a null range, as with the GIL. Checked first, since
    // type's __bases__ getter does not expect a null field.
    if (_Py_atomic_load_ptr_relaxed(field) == nullptr)
        return nullptr;
    auto *result = typeTupleFromGetter(type, bases ? PyMagicName::bases() : PyMagicName::mro());
    // The getter returns the field, so it cannot fail here
    assert(result != nullptr);
    if (result == nullptr) {
        PyErr_Clear();
        return nullptr;
    }
    if (result == Py_None) {
        Py_DECREF(result);
        return nullptr;
    }
    assert(PyTuple_Check(result) != 0);
    return result;
}

PepTypeTupleOwner::PepTypeTupleOwner(PyTypeObject *type, PepTypeTuple which)
{
    checkNoRawLock(__func__);
    const bool bases = which == PepTypeTuple::Bases;
    PyObject **field = bases ? &type->tp_bases : &type->tp_mro;
    // The getter is looked up in a dict: not with the caller's exception pending
    PyObject *pending = PyErr_GetRaisedException();
    m_typeTuple = typeTupleSnapshot(type, field, bases);
    m_owned = m_typeTuple != nullptr;
    // The counter-proof: the reference goes again while the tuple is still the type's,
    // so that the walk borrows it, as the base does
    if (m_owned && !FreeThreading::optionEnabled(FreeThreading::Option::MroSnapshot)
        && m_typeTuple == _Py_atomic_load_ptr_relaxed(field)) {
        Py_DECREF(m_typeTuple);
        m_owned = false;
    }
    if (bases)
        Shiboken::failpoint("bases-after-snapshot");
    else
        Shiboken::failpoint("mro-after-snapshot");
    PyErr_SetRaisedException(pending);
}

PepTypeTupleOwner::~PepTypeTupleOwner()
{
    if (m_owned) {
        checkNoRawLock(__func__);
        Py_DECREF(m_typeTuple);
    }
}

} // namespace Shiboken::Detail

#endif // Py_GIL_DISABLED
