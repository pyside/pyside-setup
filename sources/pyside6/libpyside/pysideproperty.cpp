// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <sbkpython.h>
#include "pysideproperty.h"
#include "pysideproperty_p.h"
#include "pysidesignal.h"
#include "pysidesignal_p.h"

#include <shiboken.h>
#include <pep384ext.h>
#include <signature.h>

using namespace Shiboken;

extern "C"
{

static PyObject *qpropertyTpNew(PyTypeObject *subtype, PyObject *args, PyObject *kwds);
static int qpropertyTpInit(PyObject *, PyObject *, PyObject *);
static void qpropertyDeAlloc(PyObject *self);

//methods
static PyObject *qPropertyGetter(PyObject *, PyObject *);
static PyObject *qPropertySetter(PyObject *, PyObject *);
static PyObject *qPropertyResetter(PyObject *, PyObject *);
static PyObject *qPropertyDeleter(PyObject *, PyObject *);
static PyObject *qPropertyCall(PyObject *, PyObject *, PyObject *);
static int qpropertyTraverse(PyObject *self, visitproc visit, void *arg);
static int qpropertyClear(PyObject *self);

// Attributes
static PyObject *qPropertyDocGet(PyObject *, void *);
static int qPropertyDocSet(PyObject *, PyObject *, void *);
static PyObject *qProperty_fget(PyObject *, void *);
static PyObject *qProperty_fset(PyObject *, void *);
static PyObject *qProperty_freset(PyObject *, void *);
static PyObject *qProperty_fdel(PyObject *, void *);

static PyMethodDef PySidePropertyMethods[] = {
    {"getter", reinterpret_cast<PyCFunction>(qPropertyGetter), METH_O, nullptr},
    {"setter", reinterpret_cast<PyCFunction>(qPropertySetter), METH_O,  nullptr},
    {"resetter", reinterpret_cast<PyCFunction>(qPropertyResetter), METH_O,  nullptr},
    {"deleter", reinterpret_cast<PyCFunction>(qPropertyDeleter), METH_O, nullptr},
    // Synonyms from Qt
    {"read", reinterpret_cast<PyCFunction>(qPropertyGetter), METH_O, nullptr},
    {"write", reinterpret_cast<PyCFunction>(qPropertySetter), METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}
};

static PyGetSetDef PySidePropertyType_getset[] = {
    // Note: we could not use `PyMemberDef` like Python's properties,
    // because of the indirection of PySidePropertyPrivate.
    {const_cast<char *>("fget"), qProperty_fget, nullptr, nullptr, nullptr},
    {const_cast<char *>("fset"), qProperty_fset, nullptr, nullptr, nullptr},
    {const_cast<char *>("freset"), qProperty_freset, nullptr, nullptr, nullptr},
    {const_cast<char *>("fdel"), qProperty_fdel, nullptr, nullptr, nullptr},
    {const_cast<char *>("__doc__"), qPropertyDocGet, qPropertyDocSet, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

static PyTypeObject *createPropertyType()
{
    PyType_Slot PySidePropertyType_slots[] = {
        {Py_tp_dealloc, reinterpret_cast<void *>(qpropertyDeAlloc)},
        {Py_tp_call, reinterpret_cast<void *>(qPropertyCall)},
        {Py_tp_traverse, reinterpret_cast<void *>(qpropertyTraverse)},
        {Py_tp_clear, reinterpret_cast<void *>(qpropertyClear)},
        {Py_tp_methods, reinterpret_cast<void *>(PySidePropertyMethods)},
        {Py_tp_init, reinterpret_cast<void *>(qpropertyTpInit)},
        {Py_tp_new, reinterpret_cast<void *>(qpropertyTpNew)},
        {Py_tp_getset, PySidePropertyType_getset},
        {Py_tp_del, reinterpret_cast<void *>(PyObject_GC_Del)},
        {0, nullptr}
    };

    PyType_Spec PySidePropertyType_spec = {
        "2:PySide6.QtCore.Property",
        sizeof(PySideProperty),
        0,
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC|Py_TPFLAGS_BASETYPE,
        PySidePropertyType_slots,
    };

    return SbkType_FromSpec(&PySidePropertyType_spec);
}

PyTypeObject *PySideProperty_TypeF(void)
{
    static auto *type = createPropertyType();
    return type;
}

PySidePropertyPrivate::PySidePropertyPrivate() noexcept = default;
PySidePropertyPrivate::~PySidePropertyPrivate() = default;

PyObject *PySidePropertyPrivate::getValue(PyObject *source) const
{
    if (fget) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        Py_INCREF(source);
        PyTuple_SET_ITEM(args, 0, source);
        return  PyObject_CallObject(fget, args);
    }
    return nullptr;
}

int PySidePropertyPrivate::setValue(PyObject *source, PyObject *value)
{
    if (fset && value) {
        Shiboken::AutoDecRef args(PyTuple_New(2));
        PyTuple_SET_ITEM(args, 0, source);
        PyTuple_SET_ITEM(args, 1, value);
        Py_INCREF(source);
        Py_INCREF(value);
        Shiboken::AutoDecRef result(PyObject_CallObject(fset, args));
        return (result.isNull() ? -1 : 0);
    }
    if (fdel) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        PyTuple_SET_ITEM(args, 0, source);
        Py_INCREF(source);
        Shiboken::AutoDecRef result(PyObject_CallObject(fdel, args));
        return (result.isNull() ? -1 : 0);
    }
    PyErr_SetString(PyExc_AttributeError, "Attribute is read only");
    return -1;
}

int PySidePropertyPrivate::reset(PyObject *source)
{
    if (freset) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        Py_INCREF(source);
        PyTuple_SET_ITEM(args, 0, source);
        Shiboken::AutoDecRef result(PyObject_CallObject(freset, args));
        return (result.isNull() ? -1 : 0);
    }
    return -1;
}

void PySidePropertyPrivate::metaCall(PyObject *source, QMetaObject::Call call, void **args)
{
    switch (call) {
    case QMetaObject::ReadProperty: {
        AutoDecRef value(getValue(source));
        auto *obValue = value.object();
        if (obValue) {
            Conversions::SpecificConverter converter(typeName);
            if (converter) {
                converter.toCpp(obValue, args[0]);
            } else {
                // PYSIDE-2160: Report an unknown type name to the caller `qtPropertyMetacall`.
                PyErr_SetObject(PyExc_StopIteration, obValue);
            }
        }
    }
        break;

    case QMetaObject::WriteProperty: {
        Conversions::SpecificConverter converter(typeName);
        if (converter) {
            AutoDecRef value(converter.toPython(args[0]));
            setValue(source, value);
        } else {
            // PYSIDE-2160: Report an unknown type name to the caller `qtPropertyMetacall`.
            PyErr_SetNone(PyExc_StopIteration);
        }
    }
        break;

    case QMetaObject::ResetProperty:
        reset(source);
        break;

    default:
        break;
    }
}

static PyObject *qpropertyTpNew(PyTypeObject *subtype, PyObject * /* args */, PyObject * /* kwds */)
{
    auto *me = PepExt_TypeCallAlloc<PySideProperty>(subtype, 0);
    me->d = new PySidePropertyPrivate;
    return reinterpret_cast<PyObject *>(me);
}

static int qpropertyTpInit(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *type{};
    auto *data = reinterpret_cast<PySideProperty *>(self);
    PySidePropertyPrivate *pData = data->d;

    static const char *kwlist[] = {"type", "fget", "fset", "freset", "fdel", "doc", "notify",
                                   "designable", "scriptable", "stored",
                                   "user", "constant", "final", nullptr};
    char *doc{};

    Py_CLEAR(pData->pyTypeObject);
    Py_CLEAR(pData->fget);
    Py_CLEAR(pData->fset);
    Py_CLEAR(pData->freset);
    Py_CLEAR(pData->fdel);
    Py_CLEAR(pData->notify);

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                                     "O|OOOOsObbbbbb:QtCore.Property",
                                     const_cast<char **>(kwlist),
                                     /*OO*/     &type, &(pData->fget),
                                     /*OOO*/    &(pData->fset), &(pData->freset), &(pData->fdel),
                                     /*s*/      &doc,
                                     /*O*/      &(pData->notify),
                                     /*bbb*/    &(pData->designable), &(pData->scriptable), &(pData->stored),
                                     /*bbb*/    &(pData->user), &(pData->constant), &(pData->final))) {
        return -1;
    }

    // PYSIDE-1019: Fetching the default `__doc__` from fget would fail for inherited functions
    // because we don't initialize the mro with signatures (and we will not!).
    // But it is efficient and in-time to do that on demand in qPropertyDocGet.
    pData->getter_doc = false;
    if (doc)
        pData->doc = doc;
    else
        pData->doc.clear();

    pData->pyTypeObject = type;
    Py_XINCREF(pData->pyTypeObject);
    pData->typeName = PySide::Signal::getTypeName(type);

    if (type == Py_None || pData->typeName.isEmpty())
        PyErr_SetString(PyExc_TypeError, "Invalid property type or type name.");
    else if (pData->constant && ((pData->fset && pData->fset != Py_None)
                                 || (pData->notify && pData->notify != Py_None)))
        PyErr_SetString(PyExc_TypeError, "A constant property cannot have a WRITE method or a "
                                         "NOTIFY signal.");
    if (!PyErr_Occurred()) {
        Py_XINCREF(pData->fget);
        Py_XINCREF(pData->fset);
        Py_XINCREF(pData->freset);
        Py_XINCREF(pData->fdel);
        Py_XINCREF(pData->notify);
        return 0;
    }
    pData->fget = nullptr;
    pData->fset = nullptr;
    pData->freset = nullptr;
    pData->fdel = nullptr;
    pData->notify = nullptr;
    return -1;
}

static void qpropertyDeAlloc(PyObject *self)
{
    qpropertyClear(self);
    // PYSIDE-939: Handling references correctly.
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(Py_TYPE(self));
    PyObject_GC_UnTrack(self);
    PepExt_TypeCallFree(self);
}

// Create a copy of the property to prevent the @property.setter from modifying
// the property in place and avoid strange side effects in derived classes
// (cf https://bugs.python.org/issue1620).
static PyObject *
_property_copy(PyObject *old, PyObject *get, PyObject *set, PyObject *reset, PyObject *del)
{
    auto *pold = reinterpret_cast<PySideProperty *>(old);
    PySidePropertyPrivate *pData = pold->d;

    AutoDecRef type(PyObject_Type(old));
    QByteArray doc{};
    if (type.isNull())
        return nullptr;

    if (get == nullptr || get == Py_None) {
        Py_XDECREF(get);
        get = pData->fget ? pData->fget : Py_None;
    }
    if (set == nullptr || set == Py_None) {
        Py_XDECREF(set);
        set = pData->fset ? pData->fset : Py_None;
    }
    if (reset == nullptr || reset == Py_None) {
        Py_XDECREF(reset);
        reset = pData->freset ? pData->freset : Py_None;
    }
    if (del == nullptr || del == Py_None) {
        Py_XDECREF(del);
        del = pData->fdel ? pData->fdel : Py_None;
    }
    // make _init use __doc__ from getter
    if ((pData->getter_doc && get != Py_None) || pData->doc.isEmpty())
        doc.clear();
    else
        doc = pData->doc;

    auto *notify = pData->notify ? pData->notify : Py_None;

    PyObject *obNew = PyObject_CallFunction(type, const_cast<char *>("OOOOOsO" "bbb" "bbb"),
        pData->pyTypeObject, get, set, reset, del, doc.data(), notify,
        pData->designable, pData->scriptable, pData->stored,
        pData->user, pData->constant, pData->final);

    return obNew;
}

static PyObject *qPropertyGetter(PyObject *self, PyObject *getter)
{
    return _property_copy(self, getter, nullptr, nullptr, nullptr);
}

static PyObject *qPropertySetter(PyObject *self, PyObject *setter)
{
    return _property_copy(self, nullptr, setter, nullptr, nullptr);
}

static PyObject *qPropertyResetter(PyObject *self, PyObject *resetter)
{
    return _property_copy(self, nullptr, nullptr, resetter, nullptr);
}

static PyObject *qPropertyDeleter(PyObject *self, PyObject *deleter)
{
    return _property_copy(self, nullptr, nullptr, nullptr, deleter);
}

static PyObject *qPropertyCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *getter = PyTuple_GetItem(args, 0);
    return _property_copy(self, getter, nullptr, nullptr, nullptr);
}

// PYSIDE-1019: Provide the same getters as Pythons `PyProperty`.

static PyObject *qProperty_fget(PyObject *self, void *)
{
    auto *func = reinterpret_cast<PySideProperty *>(self)->d->fget;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_fset(PyObject *self, void *)
{
    auto *func = reinterpret_cast<PySideProperty *>(self)->d->fset;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_freset(PyObject *self, void *)
{
    auto *func = reinterpret_cast<PySideProperty *>(self)->d->freset;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_fdel(PyObject *self, void *)
{
    auto *func = reinterpret_cast<PySideProperty *>(self)->d->fdel;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qPropertyDocGet(PyObject *self, void *)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    PySidePropertyPrivate *pData = data->d;

    QByteArray doc(pData->doc);
    if (!doc.isEmpty())
        return PyUnicode_FromString(doc);
    if (pData->fget != nullptr) {
        // PYSIDE-1019: Fetch the default `__doc__` from fget. We do it late.
        AutoDecRef get_doc(PyObject_GetAttr(pData->fget, PyMagicName::doc()));
        if (!get_doc.isNull() && get_doc.object() != Py_None) {
            pData->doc = String::toCString(get_doc);
            pData->getter_doc = true;
            if (Py_TYPE(self) == PySideProperty_TypeF())
                return qPropertyDocGet(self, nullptr);
            /*
             * If this is a property subclass, put __doc__ in dict of the
             * subclass instance instead, otherwise it gets shadowed by
             * __doc__ in the class's dict.
             */
            auto *get_doc_obj = get_doc.object();
            if (PyObject_SetAttr(self, PyMagicName::doc(), get_doc) < 0)
                return nullptr;
            Py_INCREF(get_doc_obj);
            return get_doc_obj;
        }
        PyErr_Clear();
    }
    Py_RETURN_NONE;
}

static int qPropertyDocSet(PyObject *self, PyObject *value, void *)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    PySidePropertyPrivate *pData = data->d;

    if (String::check(value)) {
        pData->doc = String::toCString(value);
        return 0;
    }
    PyErr_SetString(PyExc_TypeError, "String argument expected.");
    return -1;
}

static int qpropertyTraverse(PyObject *self, visitproc visit, void *arg)
{
    PySidePropertyPrivate *data = reinterpret_cast<PySideProperty *>(self)->d;
    if (!data)
        return 0;

    Py_VISIT(data->fget);
    Py_VISIT(data->fset);
    Py_VISIT(data->freset);
    Py_VISIT(data->fdel);
    Py_VISIT(data->notify);
    Py_VISIT(data->pyTypeObject);
    return 0;
}

static int qpropertyClear(PyObject *self)
{
    PySidePropertyPrivate *data = reinterpret_cast<PySideProperty *>(self)->d;
    if (!data)
        return 0;

    Py_CLEAR(data->fget);
    Py_CLEAR(data->fset);
    Py_CLEAR(data->freset);
    Py_CLEAR(data->fdel);
    Py_CLEAR(data->notify);
    Py_CLEAR(data->pyTypeObject);

    delete data;
    reinterpret_cast<PySideProperty *>(self)->d = nullptr;
    return 0;
}

} // extern "C"

static PyObject *getFromType(PyTypeObject *type, PyObject *name)
{
    AutoDecRef tpDict(PepType_GetDict(type));
    auto *attr = PyDict_GetItem(tpDict.object(), name);
    if (!attr) {
        PyObject *bases = type->tp_bases;
        const Py_ssize_t size = PyTuple_GET_SIZE(bases);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject *base = PyTuple_GET_ITEM(bases, i);
            attr = getFromType(reinterpret_cast<PyTypeObject *>(base), name);
            if (attr)
                return attr;
        }
    }
    return attr;
}

namespace PySide::Property {

static const char *Property_SignatureStrings[] = {
    "PySide6.QtCore.Property(self,type:type,fget:typing.Callable=None,fset:typing.Callable=None,"
        "freset:typing.Callable=None,fdel:typing.Callable=None,doc:str=None,"
        "notify:typing.Callable=None,designable:bool=True,scriptable:bool=True,"
        "stored:bool=True,user:bool=False,constant:bool=False,final:bool=False)",
    "PySide6.QtCore.Property.deleter(self,fdel:typing.Callable)->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.getter(self,fget:typing.Callable)->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.read(self,fget:typing.Callable)->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.setter(self,fset:typing.Callable)->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.write(self,fset:typing.Callable)->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.__call__(self, func:typing.Callable)->PySide6.QtCore.Property",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    if (InitSignatureStrings(PySideProperty_TypeF(), Property_SignatureStrings) < 0)
        return;

    Py_INCREF(PySideProperty_TypeF());
    PyModule_AddObject(module, "Property", reinterpret_cast<PyObject *>(PySideProperty_TypeF()));
}

bool checkType(PyObject *pyObj)
{
    if (pyObj) {
        return PyType_IsSubtype(Py_TYPE(pyObj), PySideProperty_TypeF());
    }
    return false;
}

PyObject *getValue(PySideProperty *self, PyObject *source)
{
    return self->d->getValue(source);
}

int setValue(PySideProperty *self, PyObject *source, PyObject *value)
{
    return self->d->setValue(source, value);
}

int reset(PySideProperty *self, PyObject *source)
{
    return self->d->reset(source);
}

const char *getTypeName(const PySideProperty *self)
{
    return self->d->typeName;
}

PySideProperty *getObject(PyObject *source, PyObject *name)
{
    PyObject *attr = nullptr;

    attr = getFromType(Py_TYPE(source), name);
    if (attr && checkType(attr)) {
        Py_INCREF(attr);
        return reinterpret_cast<PySideProperty *>(attr);
    }

    if (!attr)
        PyErr_Clear(); //Clear possible error caused by PyObject_GenericGetAttr

    return nullptr;
}

bool isReadable(const PySideProperty * /* self */)
{
    return true;
}

bool isWritable(const PySideProperty *self)
{
    return self->d->fset != nullptr;
}

bool hasReset(const PySideProperty *self)
{
    return self->d->freset != nullptr;
}

bool isDesignable(const PySideProperty *self)
{
    return self->d->designable;
}

bool isScriptable(const PySideProperty *self)
{
    return self->d->scriptable;
}

bool isStored(const PySideProperty *self)
{
    return self->d->stored;
}

bool isUser(const PySideProperty *self)
{
    return self->d->user;
}

bool isConstant(const PySideProperty *self)
{
    return self->d->constant;
}

bool isFinal(const PySideProperty *self)
{
    return self->d->final;
}

const char *getNotifyName(PySideProperty *self)
{
    if (self->d->notifySignature.isEmpty()) {
        AutoDecRef str(PyObject_Str(self->d->notify));
        self->d->notifySignature = Shiboken::String::toCString(str);
    }

    return self->d->notifySignature.isEmpty()
        ? nullptr : self->d->notifySignature.constData();
}

void setTypeName(PySideProperty *self, const char *typeName)
{
    self->d->typeName = typeName;
}

PyObject *getTypeObject(const PySideProperty *self)
{
    return self->d->pyTypeObject;
}

} //namespace PySide::Property
