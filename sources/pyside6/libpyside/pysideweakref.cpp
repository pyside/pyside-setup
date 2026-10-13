// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideweakref.h"

#include <basewrapper.h>
#include <sbktypefactory.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

struct PySideCallableObject {
    PyObject_HEAD
    /* Type-specific fields go here. */
    PySideWeakRefFunction weakref_func;
    void *user_data;
    bool keep_reference;
};

static PyObject *CallableObject_call(PyObject *callable_object, PyObject *args, PyObject *kw);

static PyTypeObject *createCallableObjectType()
{
    PyType_Slot PySideCallableObjectType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(CallableObject_call)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideCallableObjectType_spec = {
        "1:PySide.Callable",
        sizeof(PySideCallableObject),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideCallableObjectType_slots,
    };

    return SbkType_FromSpec(&PySideCallableObjectType_spec);
}

static PyTypeObject *PySideCallableObject_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createCallableObjectType);
#else
    static auto *type = createCallableObjectType();
#endif
    return type;
}

static PyObject *CallableObject_call(PyObject *callable_object, PyObject *args, PyObject * /* kw */)
{
    auto *obj = reinterpret_cast<PySideCallableObject *>(callable_object);
    obj->weakref_func(obj->user_data);

    // Unless the caller owns the returned reference, drop the weak ref object here
    // (it was created with a discarded reference). When the caller keeps it, it is
    // responsible for releasing it, so leave it untouched to avoid a double free.
    if (!obj->keep_reference)
        Py_XDECREF(PyTuple_GetItem(args, 0)); // kill weak ref object
    Py_RETURN_NONE;
}

/*!
    \namespace PySide::WeakRef
    \brief Namespace for Python weakref helper functions.
    \inmodule libpyside
*/
namespace PySide::WeakRef {

PyObject *create(PyObject *obj, PySideWeakRefFunction func, void *userData, bool keepReference)
{
    if (obj == Py_None)
        return nullptr;

    auto *callableObject_Type = PySideCallableObject_TypeF();
    auto *callableObject_PyObject = reinterpret_cast<PyObject *>(callableObject_Type);
    if (callableObject_PyObject->ob_type == nullptr) {
        callableObject_PyObject->ob_type = &PyType_Type;
        PyType_Ready(callableObject_Type);
    }

    PyTypeObject *type = PySideCallableObject_TypeF();
    PySideCallableObject *callable = PyObject_New(PySideCallableObject, type);
    if (!callable || PyErr_Occurred())
        return nullptr;

    PyObject *weak = PyWeakref_NewRef(obj, reinterpret_cast<PyObject *>(callable));
    if (!weak || PyErr_Occurred())
        return nullptr;

    callable->weakref_func = func;
    callable->user_data = userData;
    callable->keep_reference = keepReference;
    Py_DECREF(callable); // PYSIDE-79: after decref the callable is undefined (theoretically)

    return weak;
}

} // namespace PySide::WeakRef
