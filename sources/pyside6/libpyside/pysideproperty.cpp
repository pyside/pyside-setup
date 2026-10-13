// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include <sbkpython.h>
#include "pyobjectwrapper.h"
#include "pysideproperty.h"
#include "pysideproperty_p.h"
#include "pysidesignal.h"
#include "pysidesignal_p.h"

#include <autodecref.h>
#include <pep384ext.h>
#include <sbkconverter.h>
#include <sbktypetupleref.h>
#include <sbkstaticstrings.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <utility>

using namespace Shiboken;

using namespace Qt::StringLiterals;

Q_DECLARE_OPERATORS_FOR_FLAGS(PySide::Property::PropertyFlags)

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
    {"getter", qPropertyGetter, METH_O, nullptr},
    // "name@setter" handling
    {"setter", qPropertySetter, METH_O, nullptr},
    {"resetter", qPropertyResetter, METH_O, nullptr},
    {"deleter", qPropertyDeleter, METH_O, nullptr},
    // Synonyms from Qt
    {"read", qPropertyGetter, METH_O, nullptr},
    {"write", qPropertySetter, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}
};

static PyGetSetDef PySidePropertyType_getset[] = {
    // Note: we could not use `PyMemberDef` like Python's properties,
    // because of the indirection of PySidePropertyPrivate.
    {"fget", qProperty_fget, nullptr, nullptr, nullptr},
    {"fset", qProperty_fset, nullptr, nullptr, nullptr},
    {"freset", qProperty_freset, nullptr, nullptr, nullptr},
    {"fdel", qProperty_fdel, nullptr, nullptr, nullptr},
    {"__doc__", qPropertyDocGet, qPropertyDocSet, nullptr, nullptr},
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

PySidePropertyBase::PySidePropertyBase(Type t) : m_type(t)
{
}

PySidePropertyBase::PySidePropertyBase(const PySidePropertyBase &rhs) = default;

void PySidePropertyBase::tp_clearBase()
{
    Py_CLEAR(m_notify);
    Py_CLEAR(m_pyTypeObject);
}

int PySidePropertyBase::tp_traverseBase(visitproc visit, void *arg)
{
    Py_VISIT(m_notify);
    Py_VISIT(m_pyTypeObject);
    return 0;
}

void PySidePropertyBase::increfBase()
{
    Py_XINCREF(m_notify);
    Py_XINCREF(m_pyTypeObject);
}

PySidePropertyBase *PySidePropertyBase::clone() const
{
    Q_UNIMPLEMENTED();
    return nullptr;
}

// Helper to check a callable function passed to a property instance.
bool PySidePropertyBase::assignCheckCallable(PyObject *source, const char *name,
                                             PyObject **target)
{
    if (source != nullptr && source != Py_None) {
        if (PyCallable_Check(source) == 0) {
            PyErr_Format(PyExc_TypeError, "Non-callable parameter given for \"%s\".", name);
            return false;
        }
        *target = source;
    } else {
        *target = nullptr;
    }
    return true;
}

void PySidePropertyPrivate::tp_clear()
{
    PySidePropertyBase::tp_clearBase();
    Py_CLEAR(fget);
    Py_CLEAR(fset);
    Py_CLEAR(freset);
    Py_CLEAR(fdel);
}

int PySidePropertyPrivate::tp_traverse(visitproc visit, void *arg)
{
    Py_VISIT(fget);
    Py_VISIT(fset);
    Py_VISIT(freset);
    Py_VISIT(fdel);
    return PySidePropertyBase::tp_traverseBase(visit, arg);
}

void PySidePropertyPrivate::incref()
{
    PySidePropertyBase::increfBase();
    Py_XINCREF(fget);
    Py_XINCREF(fset);
    Py_XINCREF(freset);
    Py_XINCREF(fdel);
}

PyObject *PySidePropertyPrivate::getValue(PyObject *source) const
{
    if (fget) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        Py_INCREF(source);
        PyTuple_SetItem(args, 0, source);
        return  PyObject_CallObject(fget, args);
    }
    return nullptr;
}

int PySidePropertyPrivate::setValue(PyObject *source, PyObject *value)
{
    if (fset != nullptr && value != nullptr) {
        Shiboken::AutoDecRef args(PyTuple_New(2));
        PyTuple_SetItem(args, 0, source);
        PyTuple_SetItem(args, 1, value);
        Py_INCREF(source);
        Py_INCREF(value);
        Shiboken::AutoDecRef result(PyObject_CallObject(fset, args));
        return (result.isNull() ? -1 : 0);
    }
    if (fdel != nullptr) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        PyTuple_SetItem(args, 0, source);
        Py_INCREF(source);
        Shiboken::AutoDecRef result(PyObject_CallObject(fdel, args));
        return (result.isNull() ? -1 : 0);
    }
    PyErr_SetString(PyExc_AttributeError, "Attribute is read only");
    return -1;
}

int PySidePropertyPrivate::reset(PyObject *source)
{
    if (freset != nullptr) {
        Shiboken::AutoDecRef args(PyTuple_New(1));
        Py_INCREF(source);
        PyTuple_SetItem(args, 0, source);
        Shiboken::AutoDecRef result(PyObject_CallObject(freset, args));
        return (result.isNull() ? -1 : 0);
    }
    return -1;
}

PySidePropertyPrivate *PySidePropertyPrivate::clone() const
{
    auto *result = new PySidePropertyPrivate(*this);
    result->incref();
    return result;
}

void PySidePropertyPrivate::metaCall(PyObject *source, QMetaObject::Call call, void **args)
{
    switch (call) {
    case QMetaObject::ReadProperty: {
        AutoDecRef value(getValue(source));
        if (value.isNull())
            return;
        if (typeName() == "PyObject"_ba) {
            // Manual conversion, see PyObjectWrapper converter registration
            auto *pw = reinterpret_cast<PySide::PyObjectWrapper *>(args[0]);
            pw->reset(value.object());
            return;
        }
        if (Conversions::SpecificConverter converter(typeName()); converter) {
            converter.toCpp(value.object(), args[0]);
            return;
        }
        // PYSIDE-2160: Report an unknown type name to the caller `qtPropertyMetacall`.
        PyErr_SetObject(PyExc_StopIteration, value.object());
    }
        break;

    case QMetaObject::WriteProperty: {
        Conversions::SpecificConverter converter(typeName());
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

// Helpers & name for passing the the PySidePropertyPrivate
// as a capsule when constructing.
static const char dataCapsuleName[] = "PropertyPrivate";
static const char dataCapsuleKeyName[] = "_PropertyPrivate"; // key in keyword args

static PySidePropertyBase *getDataFromKwArgs(PyObject *kwds)
{
    if (kwds != nullptr && PyDict_Check(kwds) != 0) {
        static PyObject *key = PyUnicode_InternFromString(dataCapsuleKeyName);
        if (PyDict_Contains(kwds, key) != 0) {
            Shiboken::AutoDecRef data(PyDict_GetItem(kwds, key));
            if (PyCapsule_CheckExact(data.object()) != 0) {
                if (void *p = PyCapsule_GetPointer(data.object(), dataCapsuleName))
                    return reinterpret_cast<PySidePropertyBase *>(p);
            }
        }
    }
    return nullptr;
}

static void addDataCapsuleToKwArgs(const AutoDecRef &kwds, PySidePropertyBase *data)
{
    auto *capsule = PyCapsule_New(data, dataCapsuleName, nullptr);
    PyDict_SetItemString(kwds.object(), dataCapsuleKeyName, capsule);
}

static inline PySidePropertyPrivate *propertyPrivate(PyObject *self)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    Q_ASSERT(data->d != nullptr);
    Q_ASSERT(data->d->type() == PySidePropertyBase::Type::Property);
    return static_cast<PySidePropertyPrivate *>(data->d);
}

static PyObject *qpropertyTpNew(PyTypeObject *subtype, PyObject * /* args */, PyObject *kwds)
{
    auto *me = PepExt_TypeCallAlloc<PySideProperty>(subtype, 0);
    me->d = getDataFromKwArgs(kwds);
    if (me->d == nullptr)
        me->d = new PySidePropertyPrivate;
    return reinterpret_cast<PyObject *>(me);
}

static int qpropertyTpInit(PyObject *self, PyObject *args, PyObject *kwds)
{
    auto *pData = propertyPrivate(self);

    if (!pData->typeName().isEmpty()) // Cloned copy, already initialized
        return 0;

    static const char *kwlist[] = {"type", "fget", "fset", "freset", "fdel", "doc", "notify",
                                   "designable", "scriptable", "stored",
                                   "user", "constant",
                                   "final", "virtual", "override",
                                   dataCapsuleKeyName, nullptr};
    char *doc{};
    PyObject *type{}, *fget{}, *fset{}, *freset{}, *fdel{}, *notify{};
    PyObject *dataCapsule{};
    bool designable{true}, scriptable{true}, stored{true};
    bool user{false}, constant{false};
    bool finalProp{false}, overrideProp{false}, virtualProp{false};

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                                     "O|OOOOsObbbbbbbbO:QtCore.Property",
                                     const_cast<char **>(kwlist),
                                     /*OO*/     &type, &fget,
                                     /*OOO*/    &fset, &freset, &fdel,
                                     /*s*/      &doc,
                                     /*O*/      &notify,
                                     /*bbb*/    &designable, &scriptable, &stored,
                                     /*bb*/     &user, &constant,
                                     /*bbb*/    &finalProp, &virtualProp, &overrideProp,
                                     /*O*/      &dataCapsule)) {
        return -1;
    }

    if (!PySidePropertyPrivate::assignCheckCallable(fget, "fget", &pData->fget)
        || !PySidePropertyPrivate::assignCheckCallable(fset, "fset", &pData->fset)
        || !PySidePropertyPrivate::assignCheckCallable(freset, "freset", &pData->freset)
        || !PySidePropertyPrivate::assignCheckCallable(fdel, "fdel", &pData->fdel)) {
        pData->fget = pData->fset = pData->freset = pData->fdel = nullptr;
        pData->setNotify(nullptr);
        return -1;
    }

    if (notify != nullptr && notify != Py_None)
        pData->setNotify(notify);

    // PYSIDE-1019: Fetching the default `__doc__` from fget would fail for inherited functions
    // because we don't initialize the mro with signatures (and we will not!).
    // But it is efficient and in-time to do that on demand in qPropertyDocGet.
    pData->getter_doc = false;
    pData->setDoc(doc != nullptr ? QByteArray(doc) : QByteArray{});

    pData->setPyTypeObject(type);
    pData->setTypeName(PySide::Signal::getTypeName(type));

    PySide::Property::PropertyFlags flags;
    flags.setFlag(PySide::Property::PropertyFlag::Readable, pData->fget != nullptr);
    flags.setFlag(PySide::Property::PropertyFlag::Writable, pData->fset != nullptr);
    flags.setFlag(PySide::Property::PropertyFlag::Resettable, pData->freset != nullptr);
    flags.setFlag(PySide::Property::PropertyFlag::Designable, designable);
    flags.setFlag(PySide::Property::PropertyFlag::Scriptable, scriptable);
    flags.setFlag(PySide::Property::PropertyFlag::Stored, stored);
    flags.setFlag(PySide::Property::PropertyFlag::User, user);
    flags.setFlag(PySide::Property::PropertyFlag::Constant, constant);
    flags.setFlag(PySide::Property::PropertyFlag::Final, finalProp);
    flags.setFlag(PySide::Property::PropertyFlag::Virtual, virtualProp);
    flags.setFlag(PySide::Property::PropertyFlag::Override, overrideProp);
    pData->setFlags(flags);

    if (type == Py_None || pData->typeName().isEmpty())
        PyErr_SetString(PyExc_TypeError, "Invalid property type or type name.");
    else if (constant && pData->fset != nullptr)
        PyErr_SetString(PyExc_TypeError, "A constant property cannot have a WRITE method.");
    else if (constant && pData->notify() != nullptr)
        PyErr_SetString(PyExc_TypeError, "A constant property cannot have a NOTIFY signal.");

    if (PyErr_Occurred() != nullptr) {
        // Nothing has been increfed yet (that happens below), so drop every
        // borrowed argument, including the type, which tp_clear would otherwise
        // decref without a matching incref.
        pData->fget = pData->fset = pData->freset = pData->fdel = nullptr;
        pData->setNotify(nullptr);
        pData->setPyTypeObject(nullptr);
        return -1;
    }

    pData->incref();
    return 0;
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
// the property in place and avoid strange side effects when modifying the
// property in derived classes (cf https://bugs.python.org/issue1620,
// pysidetest/property_python_test.py).
static PyObject *copyProperty(PyObject *old)
{
    AutoDecRef type(PyObject_Type(old));
    Shiboken::AutoDecRef kwds(PyDict_New());
    addDataCapsuleToKwArgs(kwds, propertyPrivate(old)->clone());
    Shiboken::AutoDecRef args(PyTuple_New(0));
    return PyObject_Call(type.object(), args.object(), kwds.object());
}

static PyObject *qPropertyGetter(PyObject *self, PyObject *getter)
{
    PyObject *result = copyProperty(self);
    if (result != nullptr) {
        auto *data = propertyPrivate(result);
        auto *old = std::exchange(data->fget, getter);
        Py_XINCREF(data->fget);
        Py_XDECREF(old);
        data->setFlag(PySide::Property::PropertyFlag::Readable);
    }
    return result;
}

static PyObject *qPropertySetter(PyObject *self, PyObject *setter)
{
    PyObject *result = copyProperty(self);
    if (result != nullptr) {
        auto *data = propertyPrivate(result);
        auto *old = std::exchange(data->fset, setter);
        Py_XINCREF(data->fset);
        Py_XDECREF(old);
        data->setFlag(PySide::Property::PropertyFlag::Writable);
    }
    return result;
}

static PyObject *qPropertyResetter(PyObject *self, PyObject *resetter)
{
    PyObject *result = copyProperty(self);
    if (result != nullptr) {
        auto *data = propertyPrivate(result);
        auto *old = std::exchange(data->freset, resetter);
        Py_XINCREF(data->freset);
        Py_XDECREF(old);
        data->setFlag(PySide::Property::PropertyFlag::Resettable);
    }
    return result;
}

static PyObject *qPropertyDeleter(PyObject *self, PyObject *deleter)
{
    PyObject *result = copyProperty(self);
    if (result != nullptr) {
        auto *data = propertyPrivate(result);
        auto *old = std::exchange(data->fdel, deleter);
        Py_XINCREF(data->fdel);
        Py_XDECREF(old);
    }
    return result;
}

static PyObject *qPropertyCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *getter = PyTuple_GetItem(args, 0);
    return qPropertyGetter(self, getter);
}

// PYSIDE-1019: Provide the same getters as Pythons `PyProperty`.

static PyObject *qProperty_fget(PyObject *self, void *)
{
    auto *func = propertyPrivate(self)->fget;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_fset(PyObject *self, void *)
{
    auto *func = propertyPrivate(self)->fset;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_freset(PyObject *self, void *)
{
    auto *func = propertyPrivate(self)->freset;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qProperty_fdel(PyObject *self, void *)
{
    auto *func = propertyPrivate(self)->fdel;
    if (func == nullptr)
        Py_RETURN_NONE;
    Py_INCREF(func);
    return func;
}

static PyObject *qPropertyDocGet(PyObject *self, void *)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    if (!data->d->doc().isEmpty() || data->d->type() != PySidePropertyBase::Type::Property)
        return PyUnicode_FromString(data->d->doc());

    auto *pData = static_cast<PySidePropertyPrivate *>(data->d);
    if (pData->fget != nullptr) {
        // PYSIDE-1019: Fetch the default `__doc__` from fget. We do it late.
        AutoDecRef get_doc(PyObject_GetAttr(pData->fget, PyMagicName::doc()));
        if (!get_doc.isNull() && get_doc.object() != Py_None) {
            pData->setDoc(String::toCString(get_doc));
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
    if (String::check(value)) {
        data->d->setDoc(String::toCString(value));
        return 0;
    }
    PyErr_SetString(PyExc_TypeError, "String argument expected.");
    return -1;
}

static int qpropertyTraverse(PyObject *self, visitproc visit, void *arg)
{
    auto *pData = propertyPrivate(self);
    return pData != nullptr ? pData->tp_traverse(visit, arg) : 0;
}

static int qpropertyClear(PyObject *self)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    if (data->d == nullptr)
        return 0;

    auto *baseData = std::exchange(data->d, nullptr);
    Q_ASSERT(baseData->type() == PySidePropertyBase::Type::Property);
    static_cast<PySidePropertyPrivate *>(baseData)->tp_clear();
    delete baseData;
    return 0;
}

} // extern "C"

static PyObject *getFromType(PyTypeObject *type, PyObject *name)
{
    AutoDecRef tpDict(PepType_GetDict(type));
    auto *attr = PyDict_GetItem(tpDict.object(), name);
    if (!attr) {
        PepBasesRef basesRef(type);
        for (PyTypeObject *base : basesRef) {
            attr = getFromType(base, name);
            if (attr)
                return attr;
        }
    }
    return attr;
}

/*!
    \namespace PySide::Property
    \brief Namespace for type @Property.
    \inmodule libpyside
*/
namespace PySide::Property {

static const char *Property_SignatureStrings[] = {
    "PySide6.QtCore.Property(self,type:type,"
        "fget:typing.Optional[collections.abc.Callable[[typing.Any],typing.Any]]=None,"
        "fset:typing.Optional[collections.abc.Callable[[typing.Any,typing.Any],None]]=None,"
        "freset:typing.Optional[collections.abc.Callable[[typing.Any,typing.Any],None]]=None,"
        "doc:str=None,"
        "notify:typing.Optional[PySide6.QtCore.Signal]=None,"
        "designable:bool=True,scriptable:bool=True,"
        "stored:bool=True,user:bool=False,constant:bool=False,final:bool=False)",
    "PySide6.QtCore.Property.deleter(self,fdel:collections.abc.Callable[[typing.Any],None])->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.getter(self,fget:collections.abc.Callable[[typing.Any],typing.Any])->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.read(self,fget:collections.abc.Callable[[typing.Any],typing.Any])->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.setter(self,fset:collections.abc.Callable[[typing.Any,typing.Any],None])->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.write(self,fset:collections.abc.Callable[[typing.Any,typing.Any],None])->PySide6.QtCore.Property",
    "PySide6.QtCore.Property.__call__(self, func:collections.abc.Callable[...,typing.Any])->PySide6.QtCore.Property",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *propertyType = PySideProperty_TypeF();
    if (InitSignatureStrings(propertyType, Property_SignatureStrings) < 0)
        return;

    auto *obPropertyType = reinterpret_cast<PyObject *>(propertyType);
    Py_INCREF(obPropertyType);
    PepModule_AddType(module, propertyType);
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
    return static_cast<PySidePropertyPrivate *>(self->d)->getValue(source);
}

int setValue(PySideProperty *self, PyObject *source, PyObject *value)
{
    return static_cast<PySidePropertyPrivate *>(self->d)->setValue(source, value);
}

int reset(PySideProperty *self, PyObject *source)
{
    return static_cast<PySidePropertyPrivate *>(self->d)->reset(source);
}

const char *getTypeName(const PySideProperty *self)
{
    return self->d->typeName().constData();
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

const char *getNotifyName(PySideProperty *self)
{
    if (self->d->notifySignature().isEmpty()) {
        AutoDecRef str(PyObject_Str(self->d->notify()));
        self->d->setNotifySignature(Shiboken::String::toCString(str));
    }

    return self->d->notifySignature().isEmpty()
        ? nullptr : self->d->notifySignature().constData();
}

void setTypeName(PySideProperty *self, const char *typeName)
{
    self->d->setTypeName(typeName);
}

PyObject *getTypeObject(const PySideProperty *self)
{
    return self->d->pyTypeObject();
}

PyObject *create(const char *typeName, PyObject *getter,
                 PyObject *setter, PyObject *notifySignature,
                 PySidePropertyBase *data)
{
    Shiboken::AutoDecRef kwds(PyDict_New());
    PyDict_SetItemString(kwds.object(), "type", PyUnicode_FromString(typeName));
    if (data != nullptr)
        addDataCapsuleToKwArgs(kwds, data);
    if (getter != nullptr && getter != Py_None)
        PyDict_SetItemString(kwds.object(), "fget", getter);
    if (setter != nullptr && getter != Py_None)
        PyDict_SetItemString(kwds.object(), "fset", setter);
    if (notifySignature != nullptr && notifySignature != Py_None)
        PyDict_SetItemString(kwds.object(), "notify", notifySignature);

    // Create PySideProperty
    Shiboken::AutoDecRef args(PyTuple_New(0));
    PyObject *result = PyObject_Call(reinterpret_cast<PyObject *>(PySideProperty_TypeF()),
                                     args, kwds.object());
    if (result == nullptr || PyErr_Occurred() != nullptr)
        return nullptr;
    return result;
}

PyObject *create(const char *typeName, PyObject *getter,
                 PyObject *setter, const char *notifySignature,
                 PySidePropertyBase *data)
{

    PyObject *obNotifySignature = notifySignature != nullptr
        ? PyUnicode_FromString(notifySignature) : nullptr;
    PyObject *result = create(typeName, getter, setter, obNotifySignature, data);
    Py_XDECREF(obNotifySignature);
    return result;
}

} //namespace PySide::Property
