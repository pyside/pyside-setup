// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "class_property.h"
#include "feature_select.h"

#include <basewrapper.h>
#include <pep384ext.h>
#include <sbkstaticstrings.h>
#include <sbktypefactory.h>
#include <signature.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

extern "C" {

/*
 * A `classproperty` is the same as a `property` but the `__get__()` and `__set__()`
 * methods are modified to always use the object class instead of a concrete instance.
 *
 * Note: A "static property" as it is often called does not exist per se.
 * Static methods do not receive anything when created. Static methods which
 * should participate in a property must be turned into class methods, before.
 * See function `createProperty` in `feature_select.cpp`.
 */

// `class_property.__get__()`: Always pass the class instead of the instance.
static PyObject *PyClassProperty_descr_get(PyObject *self, PyObject * /*ob*/, PyObject *cls)
{
    return PepExt_Type_GetDescrGetSlot(&PyProperty_Type)(self, cls, cls);
}

// `class_property.__set__()`: Just like the above `__get__()`.
static int PyClassProperty_descr_set(PyObject *self, PyObject *obj, PyObject *value)
{
    PyObject *cls = PyType_Check(obj) ? obj : reinterpret_cast<PyObject *>(Py_TYPE(obj));
    return PepExt_Type_GetDescrSetSlot(&PyProperty_Type)(self, cls, value);
}

static PyTypeObject *createClassPropertyTypeType()
{
    PyType_Slot PyClassPropertyType_Type_slots[] = {
        {Py_tp_base, static_cast<void *>(&PyType_Type)},
        {Py_tp_alloc, reinterpret_cast<void *>(PyType_GenericAlloc)},
        {Py_tp_free, reinterpret_cast<void *>(PyObject_GC_Del)},
        {0, nullptr}
    };

    PyType_Spec PyClassPropertyType_Type_spec = {
        "1:Shiboken.ClassPropertyType",
        0,
        0, // sizeof(PyMemberDef), not for PyPy without a __len__ defined
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_TYPE_SUBCLASS,
        PyClassPropertyType_Type_slots,
    };

    return SbkType_FromSpec(&PyClassPropertyType_Type_spec);
}

PyTypeObject *PyClassPropertyType_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createClassPropertyTypeType);
#else
    static auto *type = createClassPropertyTypeType();
#endif
    return type;
}

// The property `__doc__` default does not work for class properties
// because PyProperty_Type.tp_init thinks this is a subclass which needs PyObject_SetAttr.
// We call `__init__` while pretending to be a PyProperty_Type instance.
static int PyClassProperty_tp_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    auto *hold = Py_TYPE(self);
    self->ob_type = &PyProperty_Type;
    auto ret = PepExt_Type_GetInitSlot(&PyProperty_Type)(self, args, kwargs);
    self->ob_type = hold;
    return ret;
}

static PyTypeObject *createPyClassPropertyType()
{
    PyType_Slot PyClassProperty_slots[] = {
        {Py_tp_getset,      reinterpret_cast<void *>(PyProperty_Type.tp_getset)},    // will be set below
        {Py_tp_base,        reinterpret_cast<void *>(&PyProperty_Type)},
        {Py_tp_descr_get,   reinterpret_cast<void *>(PyClassProperty_descr_get)},
        {Py_tp_descr_set,   reinterpret_cast<void *>(PyClassProperty_descr_set)},
        {Py_tp_init,        reinterpret_cast<void *>(PyClassProperty_tp_init)},
        {0, nullptr}
    };

    PyType_Spec PyClassProperty_spec = {
        "2:PySide6.QtCore.PyClassProperty",
        sizeof(propertyobject),
        0,
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
        PyClassProperty_slots,
    };

    return SbkType_FromSpecWithMeta(&PyClassProperty_spec, PyClassPropertyType_TypeF());
}

PyTypeObject *PyClassProperty_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createPyClassPropertyType);
#else
    static auto *type = createPyClassPropertyType();
#endif
    return type;
}

/*
 * Types with class properties need to handle `Type.class_prop = x` in a specific way.
 * By default, Python replaces the `class_property` itself, but for wrapped C++ types
 * we need to call `class_property.__set__()` in order to propagate the new value to
 * the underlying C++ data structure.
 */
static int SbkObjectType_meta_setattro(PyObject *obj, PyObject *name, PyObject *value)
{
    // Use `_PepType_Lookup()` instead of `PyObject_GetAttr()` in order to get the raw
    // descriptor (`property`) instead of calling `tp_descr_get` (`property.__get__()`).
    auto *type = reinterpret_cast<PyTypeObject *>(obj);
    PySide::Feature::Select(type);
    PyObject *descr = _PepType_Lookup(type, name);

    // The following assignment combinations are possible:
    //   1. `Type.class_prop = value`              --> descr_set: `Type.class_prop.__set__(value)`
    //   2. `Type.class_prop = other_class_prop`   --> setattro:  replace existing `class_prop`
    //   3. `Type.regular_attribute = value`       --> setattro:  regular attribute assignment
    auto *class_prop = reinterpret_cast<PyObject *>(PyClassProperty_TypeF());
    const auto call_descr_set = descr && PyObject_IsInstance(descr, class_prop)
                                && !PyObject_IsInstance(value, class_prop);
    if (call_descr_set) {
        // Call `class_property.__set__()` instead of replacing the `class_property`.
        return PepExt_Type_GetDescrSetSlot(Py_TYPE(descr))(descr, obj, value);
    } // Replace existing attribute.
#ifdef Py_GIL_DISABLED
    Shiboken::ObjectType::ClassWrite write(type); // For the signal lists, see pysidesignal.cpp
#endif
    return PepExt_Type_GetSetAttroSlot(&PyType_Type)(obj, name, value);
}

} // extern "C"

/*
 * These functions are added to the SbkObjectType_TypeF() dynamically.
 */

/*!
    \namespace PySide::ClassProperty
    \brief Namespace for the class property metaclass.

     PYSIDE-2230: Why is this metaclass necessary? The problem is that the
     property object already exists as a Python object. We derive a subclass
     for class properties, without repeating everything but just by adding
     something to support the class-ness.

     But this Python property has as metaclass `type` which is incompatible now
     with SbkObjectType, which generates physically larger types that are
     incompatible with properties by using PEP 697. Adding a compatible
     metaclass that is unrelated to `SbkObjectType` is the correct solution.
     Re-using `SbkObjectType` was actually an abuse, since Python properties
     are in no way PySide objects.

    \inmodule libpyside
*/
namespace PySide::ClassProperty {

static const char *PyClassProperty_SignatureStrings[] = {
    "PySide6.QtCore.PyClassProperty(cls,"
        "fget:typing.Optional[collections.abc.Callable[[typing.Any],typing.Any]]=None,"
        "fset:typing.Optional[collections.abc.Callable[[typing.Any,typing.Any],None]]=None,"
        "fdel:typing.Optional[collections.abc.Callable[[typing.Any],None]]=None,"
        "doc:typing.Optional[str]=None)",
    "PySide6.QtCore.PyClassProperty.getter(cls,fget:collections.abc.Callable[[typing.Any],typing.Any])->PySide6.QtCore.PyClassProperty",
    "PySide6.QtCore.PyClassProperty.setter(cls,fset:collections.abc.Callable[[typing.Any,typing.Any],None])->PySide6.QtCore.PyClassProperty",
    "PySide6.QtCore.PyClassProperty.deleter(cls,fdel:collections.abc.Callable[[typing.Any],None])->PySide6.QtCore.PyClassProperty",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    PyTypeObject *type = SbkObjectType_TypeF();
    type->tp_setattro = SbkObjectType_meta_setattro;

    auto *classPropertyType = PyClassProperty_TypeF();
    if (InitSignatureStrings(classPropertyType, PyClassProperty_SignatureStrings) < 0)
        return;

    auto *obClassPropertyType = reinterpret_cast<PyObject *>(classPropertyType);
    Py_INCREF(obClassPropertyType);
    PepModule_AddType(module, classPropertyType);
}

} // namespace PySide::ClassProperty
