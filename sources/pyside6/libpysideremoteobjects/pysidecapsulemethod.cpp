// Copyright (C) 2025 Ford Motor Company
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidecapsulemethod_p.h"

#include <cstring>
#include <sbkpep.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif


/*!
    \group pysidecapsule
    \title Capsule code
    \inmodule libpysideremoteobjects

    This code is needed to solve, in C++ and adhering to the stable API,
    creating what are in effect lambda functions as instance methods on custom
    types. The goal is to be able to add methods to a dynamic type. If the .rep
    file defines a slot `mySlot`, it need to be added to the dynamic type. For
    Source types, this should be an abstract method that raises a
    NotImplementedError unless defined in the Python subclass. For Replica
    types, this should include an implementation that forwards the request
    through the underlying QRemoteObjectReplica instance.

    The stable API doesn't currently provide a way define a method that can
    receive both the `self`, `args`, and runtime (but constant per method, i.e.,
    lambda like) data using Py_tp_methods. Possibly post 3.13 when METH_METHOD is
    part of the stable API. But for now, it is not.

    The solution is to create a custom descriptor
    (https://docs.python.org/3/howto/descriptor.html) that can hold the runtime
    data and then when called, will return a PyCFunction_New generated PyObject
    that is passed both class instance `self` and the runtime data (a PyCapsule)
    together as a tuple as a new `self` for the method. The static method
    definition needs to expect and handle this, but when combined in C++, we can
    define a single handler that receives both the original `self` of the instance
    and the runtime capsule with data for handling.
*/

extern "C"
{

/*!
    \struct CapsuleDescriptorData

    The CapsuleDescriptorData struct is what will be passed as the pseudo `self`
    from a CapsuleMethod or CapsuleProperty to the associated handler method. The
    handler method (which should look like a standard PyMethodDef method) should
    parse it into the payload (the "lambda variables") and the actual instance
    (the "self"). On free-threaded builds, the pseudo \c self is a tuple of
    the instance and the capsule; see capsuleMethodPayload().

    \ingroup pysidecapsule
*/

/*!
    \struct CapsuleDescriptor

    \brief This struct is used for both CapsuleMethod and CapsuleProperty.

    \ingroup pysidecapsule
*/
struct CapsuleDescriptor
{
    PyTypeObject base;
    PyObject *capsule;
    PyMethodDef methodDef;

    void configure(PyObject *capsule, PyMethodDef *method)
    {
        this->capsule = capsule;
        Py_INCREF(capsule);
        // We make a copy of the input name and doc strings so they can be temporary on
        // the input.
        if (method->ml_name)
            methodDef.ml_name = strdup(method->ml_name);
        if (method->ml_doc)
            methodDef.ml_doc = strdup(method->ml_doc);
        methodDef.ml_meth = method->ml_meth;
        methodDef.ml_flags = method->ml_flags;
    }
};

static PyObject *CapsuleDescriptor_tp_new(PyTypeObject *type, PyObject * /* args */, PyObject * /* kwds */);
static void CapsuleDescriptor_free(PyObject *self);
static PyObject *CapsuleMethod_descr_get(PyObject *self, PyObject *instance, PyObject * /* owner */);
static PyObject *CapsuleProperty_descr_get(PyObject *self, PyObject *instance, PyObject * /* owner */);
static int CapsuleProperty_descr_set(PyObject *self, PyObject *instance, PyObject * /* owner */);

/**
 * We are creating two related types, CapsuleMethod and CapsuleProperty, that are
 * used to enable lambda-like behavior. The difference is in usage, where
 * CapsuleMethod's __get__ function returns a Callable (i.e., method-like usage:
 * obj.capsuleMethodName(args)) and only supports the __get__ method.
 * CapsuleProperty on the other hand is used for properties, and supports both
 * __get__ and __set__ methods (i.e., obj.capsulePropertyName = value or val =
 * obj.capsulePropertyName).
 */
static PyTypeObject *createCapsuleMethodType()
{
    PyType_Slot CapsuleMethodType_slots[] = {
        {Py_tp_new,         reinterpret_cast<void *>(CapsuleDescriptor_tp_new)},
        {Py_tp_descr_get,   reinterpret_cast<void *>(CapsuleMethod_descr_get)},
        {Py_tp_free,        reinterpret_cast<void *>(CapsuleDescriptor_free)},
        {0, nullptr}
    };

    PyType_Spec CapsuleMethodType_spec = {
        "2:PySide6.QtRemoteObjects.CapsuleMethod",
        sizeof(CapsuleDescriptor),
        0,
        Py_TPFLAGS_DEFAULT,
        CapsuleMethodType_slots};

    PyObject *type = PyType_FromSpec(&CapsuleMethodType_spec);
    if (!type) {
        PyErr_Print();
        return nullptr;
    }
    return reinterpret_cast<PyTypeObject*>(type);
}

PyTypeObject *CapsuleMethod_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createCapsuleMethodType);
#else
    static auto *type = createCapsuleMethodType();
#endif
    return type;
}

static PyTypeObject *createCapsulePropertyType(bool isWritable)
{
    PyType_Slot WritablePropertyType_slots[] = {
        {Py_tp_new,         reinterpret_cast<void *>(CapsuleDescriptor_tp_new)},
        {Py_tp_descr_get,   reinterpret_cast<void *>(CapsuleProperty_descr_get)},
        {Py_tp_descr_set,   reinterpret_cast<void *>(CapsuleProperty_descr_set)},
        {Py_tp_free,        reinterpret_cast<void *>(CapsuleDescriptor_free)},
        {0, nullptr}
    };

    PyType_Slot ReadOnlyPropertyType_slots[] = {
        {Py_tp_new,         reinterpret_cast<void *>(CapsuleDescriptor_tp_new)},
        {Py_tp_descr_get,   reinterpret_cast<void *>(CapsuleProperty_descr_get)},
        {Py_tp_free,        reinterpret_cast<void *>(CapsuleDescriptor_free)},
        {0, nullptr}
    };

    PyType_Spec CapsulePropertyType_spec = {
        "2:PySide6.QtRemoteObjects.CapsuleProperty",
        sizeof(CapsuleDescriptor),
        0,
        Py_TPFLAGS_DEFAULT,
        isWritable ? WritablePropertyType_slots : ReadOnlyPropertyType_slots};

    PyObject *type = PyType_FromSpec(&CapsulePropertyType_spec);
    if (!type) {
        PyErr_Print();
        return nullptr;
    }
    return reinterpret_cast<PyTypeObject*>(type);
}

PyTypeObject *CapsuleProperty_TypeF(bool isWritable=false)
{
    if (isWritable) {
#ifdef Py_GIL_DISABLED
        static Shiboken::OnceValue<PyTypeObject *> typeOnce;
        PyTypeObject *type = typeOnce.get([] { return createCapsulePropertyType(true); });
#else
        static auto *type = createCapsulePropertyType(true);
#endif
        return type;
    }
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get([] { return createCapsulePropertyType(false); });
#else
    static auto *type = createCapsulePropertyType(false);
#endif
    return type;
}

static PyObject *CapsuleDescriptor_tp_new(PyTypeObject *type, PyObject * /* args */, PyObject * /* kwds */)
{
    auto *self = reinterpret_cast<CapsuleDescriptor *>(PyType_GenericAlloc(type, 0));
    if (self != nullptr) {
        self->capsule = nullptr;
        self->methodDef = {nullptr, nullptr, METH_NOARGS, nullptr}; // Initialize methodDef
    }
    return reinterpret_cast<PyObject *>(self);
}

static void CapsuleDescriptor_free(PyObject *self)
{
    auto *d = reinterpret_cast<CapsuleDescriptor *>(self);
    Py_XDECREF(d->capsule);
    free(const_cast<char*>(d->methodDef.ml_name));
    free(const_cast<char*>(d->methodDef.ml_doc));
}

#ifdef Py_GIL_DISABLED
// self is a tuple of instance and payload: a bound method holds its instance
static PyObject *newCapsuleMethod(PyObject *descriptor, PyObject *instance)
{
    auto *d = reinterpret_cast<CapsuleDescriptor *>(descriptor);
    auto *data = new CapsuleDescriptorData{instance, d->capsule};
    PyObject *payload = PyCapsule_New(data, "Payload", [](PyObject *capsule) {
        delete reinterpret_cast<CapsuleDescriptorData *>(PyCapsule_GetPointer(capsule, "Payload"));
    });
    if (!payload) {
        delete data;
        return nullptr;
    }
    PyObject *self = PyTuple_Pack(2, instance, payload);
    Py_DECREF(payload);
    if (!self)
        return nullptr;
    PyObject *method = PyCFunction_New(&d->methodDef, self);
    Py_DECREF(self);
    return method;
}

/*!
    \group freethreading
    \title Free-threaded builds
    \inmodule libpysideremoteobjects

    \brief What libpysideremoteobjects adds when Python runs without a GIL.

    Only where \c Py_GIL_DISABLED is defined.

    A capsule method handler receives as \c self a tuple of the instance and
    the payload capsule, so that a bound method holds its instance.
*/

/*!
    \fn PyObject *capsuleMethodPayload(PyObject *self)
    \ingroup freethreading
    \inmodule libpysideremoteobjects

    Returns the payload capsule from the \a self tuple of a capsule method
    handler, borrowed.
*/
PyObject *capsuleMethodPayload(PyObject *self)
{
    return PyTuple_GetItem(self, 1);
}
#endif // Py_GIL_DISABLED

static PyObject *CapsuleMethod_descr_get(PyObject *self, PyObject *instance, PyObject * /* owner */)
{
    if (instance == nullptr) {
        // Return the descriptor object if accessed from the class
        Py_INCREF(self);
        return self;
    }

#ifdef Py_GIL_DISABLED
    return newCapsuleMethod(self, instance);
#else
    auto *d = reinterpret_cast<CapsuleDescriptor *>(self);
    auto *data = new CapsuleDescriptorData{instance, d->capsule};
    PyObject *payload = PyCapsule_New(data, "Payload", [](PyObject *capsule) {
        delete reinterpret_cast<CapsuleDescriptorData *>(PyCapsule_GetPointer(capsule, "Payload"));
    });
    if (!payload)
        return nullptr;

    Py_INCREF(payload);
    return PyCFunction_New(&d->methodDef, payload);
#endif // Py_GIL_DISABLED
}

bool add_capsule_method_to_type(PyTypeObject *type, PyMethodDef *method, PyObject *capsule)
{
    if (PyType_Ready(type) < 0) {
        PyErr_Print();
        return false;
    }
    auto *descriptor = reinterpret_cast<CapsuleDescriptor *>(
        PyObject_CallObject(reinterpret_cast<PyObject *>(CapsuleMethod_TypeF()), nullptr));
    if (!descriptor) {
        PyErr_Print();
        return false;
    }
    descriptor->configure(capsule, method);

    auto *descr = reinterpret_cast<PyObject *>(descriptor);
    if (PyObject_SetAttrString(reinterpret_cast<PyObject *>(type), method->ml_name, descr) < 0) {
        PyErr_Print();
        return false;
    }
    return true;
}

static PyObject *CapsuleProperty_descr_get(PyObject *self, PyObject *instance, PyObject * /* owner */)
{
    if (instance == nullptr) {
        // Return the descriptor object if accessed from the class
        Py_INCREF(self);
        return self;
    }

#ifdef Py_GIL_DISABLED
    PyObject *method = newCapsuleMethod(self, instance);
    if (!method)
        return nullptr;
    PyObject *result = PyObject_CallFunctionObjArgs(method, nullptr);
    Py_DECREF(method);
    return result;
#else
    auto *d = reinterpret_cast<CapsuleDescriptor *>(self);
    auto *data = new CapsuleDescriptorData{instance, d->capsule};
    PyObject *payload = PyCapsule_New(data, "Payload", [](PyObject *capsule) {
        delete reinterpret_cast<CapsuleDescriptorData *>(PyCapsule_GetPointer(capsule, "Payload"));
    });
    if (!payload)
        return nullptr;

    return PyObject_CallFunctionObjArgs(PyCFunction_New(&d->methodDef, payload), nullptr);
#endif // Py_GIL_DISABLED
}

static int CapsuleProperty_descr_set(PyObject *self, PyObject *instance, PyObject *value)
{
#ifdef Py_GIL_DISABLED
    PyObject *method = newCapsuleMethod(self, instance);
    if (!method)
        return -1;
    PyObject *result = PyObject_CallFunctionObjArgs(method, value, nullptr);
    Py_DECREF(method);
#else
    auto *d = reinterpret_cast<CapsuleDescriptor *>(self);
    auto *data = new CapsuleDescriptorData{instance, d->capsule};
    PyObject *payload = PyCapsule_New(data, "Payload", [](PyObject *capsule) {
        delete reinterpret_cast<CapsuleDescriptorData *>(PyCapsule_GetPointer(capsule, "Payload"));
    });
    if (!payload)
        return -1;

    Py_INCREF(payload);
    PyObject *result = PyObject_CallFunctionObjArgs(PyCFunction_New(&d->methodDef, payload),
                                                    value, nullptr);
#endif // Py_GIL_DISABLED
    if (!result)
        return -1;

    Py_DECREF(result);
    return 0;
}

// Returns a new CapsuleProperty descriptor object for use with PySideProperty
PyObject *make_capsule_property(PyMethodDef *method, PyObject *capsule, bool isWritable)
{
    auto *type = CapsuleProperty_TypeF(isWritable);
    auto *descriptor = PyObject_CallObject(reinterpret_cast<PyObject *>(type), nullptr);
    if (!descriptor)
        return nullptr;

    reinterpret_cast<CapsuleDescriptor*>(descriptor)->configure(capsule, method);

    return descriptor;
}

} // extern "C"
