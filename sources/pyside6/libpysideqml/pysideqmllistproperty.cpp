// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmllistproperty_p.h"
#include "pysideqmlregistertype_p.h"
#include "pysideqmllistpropertymixin.h"

#include <autodecref.h>
#include <gilstate.h>
#include <pep384ext.h>
#include <sbkconverter.h>
#include <signature.h>
#include <sbkpep.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <sbktypetupleref.h>

#include <pysideproperty.h>
#include <pysidepropertybase_p.h>
#include <pysideqobject.h>

#include <QtCore/qobject.h>
#include <QtQml/qqmllist.h>

#include <utility>

using namespace Qt::StringLiterals;

// This is the user data we store in the property.
class QmlListPropertyPrivate : public PySidePropertyBase, public QmlListPropertyMixin
{
public:
    Q_DISABLE_COPY_MOVE(QmlListPropertyPrivate)

    QmlListPropertyPrivate() : PySidePropertyBase(Type::ListProperty) {}
    ~QmlListPropertyPrivate() override = default;

    void metaCall(PyObject *source, QMetaObject::Call call, void **args) override
    { handleMetaCall(source, call, args); }

    qsizetype count(QQmlListProperty<QObject> *propList) const override;
    QObject *at(QQmlListProperty<QObject> *propList, qsizetype index) const override;

    void append(QQmlListProperty<QObject> *propList, QObject *item) override;
    void clear(QQmlListProperty<QObject> * propList) override;
    void replace(QQmlListProperty<QObject> *propList, qsizetype index, QObject *value) override;
    void removeLast(QQmlListProperty<QObject> *propList) override;

    void tp_clear();
    int tp_traverse(visitproc visit, void *arg);
    void incref();

    PyObject *obElementType = nullptr;
    PyObject *obAppend = nullptr;
    PyObject *obCount = nullptr;
    PyObject *obAt = nullptr;
    PyObject *obClear = nullptr;
    PyObject *obReplace = nullptr;
    PyObject *obRemoveLast = nullptr;
};

void QmlListPropertyPrivate::tp_clear()
{
    PySidePropertyBase::tp_clearBase();
    Py_CLEAR(obElementType);
    Py_CLEAR(obAppend);
    Py_CLEAR(obCount);
    Py_CLEAR(obAt);
    Py_CLEAR(obClear);
    Py_CLEAR(obReplace);
    Py_CLEAR(obRemoveLast);
}

int QmlListPropertyPrivate::tp_traverse(visitproc visit, void *arg)
{
    Py_VISIT(obElementType);
    Py_VISIT(obAppend);
    Py_VISIT(obCount);
    Py_VISIT(obAt);
    Py_VISIT(obClear);
    Py_VISIT(obReplace);
    Py_VISIT(obRemoveLast);
    return PySidePropertyBase::tp_traverseBase(visit, arg);
}

void QmlListPropertyPrivate::incref()
{
    PySidePropertyBase::increfBase();
    Py_XINCREF(obElementType);
    Py_XINCREF(obAppend);
    Py_XINCREF(obCount);
    Py_XINCREF(obAt);
    Py_XINCREF(obClear);
    Py_XINCREF(obReplace);
    Py_XINCREF(obRemoveLast);
}

static inline QmlListPropertyPrivate *qmlListProperty(PyObject *self)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    Q_ASSERT(data->d != nullptr);
    Q_ASSERT(data->d->type() == PySidePropertyBase::Type::ListProperty);
    return static_cast<QmlListPropertyPrivate *>(data->d);
}

extern "C"
{

static PyObject *propList_tp_new(PyTypeObject *subtype, PyObject * /* args */, PyObject * /* kwds */)
{
    auto *me = PepExt_TypeCallAlloc<PySideProperty>(subtype, 0);
    me->d = new QmlListPropertyPrivate;
    return reinterpret_cast<PyObject *>(me);
}

static int propListTpInit(PyObject *self, PyObject *args, PyObject *kwds)
{
    static const char *kwlist[] = {"type", "append", "count", "at", "clear",
                                   "replace", "removeLast",
                                   "doc", "notify", // PySideProperty
                                   "designable", "scriptable", "stored",
                                   "user", "constant",
                                   "final", "virtual", "override",
                                   nullptr};
    auto *pySelf = reinterpret_cast<PySideProperty *>(self);

    auto *data = static_cast<QmlListPropertyPrivate *>(pySelf->d);

    char *doc{};
    PyObject *append{}, *count{}, *at{}, *clear{}, *replace{}, *removeLast{}, *notify{};
    bool designable{true}, scriptable{true}, stored{true};
    bool user{false}, constant{false};
    bool finalProp{false}, overrideProp{false}, virtualProp{false};

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                                     "O|OOOOOOsObbbbbbbb:QtQml.ListProperty",
                                     const_cast<char **>(kwlist),
                                     &data->obElementType,
                                     &append, &count, &at, &clear, &replace, &removeLast,
                                     /*s*/   &doc,
                                     /*O*/   &notify, // PySideProperty
                                     /*bbb*/ &designable, &scriptable, &stored,
                                     /*bb*/  &user, &constant,
                                     /*bbb*/ &finalProp, &virtualProp, &overrideProp)) {
        return -1;
    }

    if (!PySidePropertyBase::assignCheckCallable(append, "append", &data->obAppend)
        || !PySidePropertyBase::assignCheckCallable(count, "count", &data->obCount)
        || !PySidePropertyBase::assignCheckCallable(at, "at", &data->obAt)
        || !PySidePropertyBase::assignCheckCallable(clear, "clear", &data->obClear)
        || !PySidePropertyBase::assignCheckCallable(replace, "replace", &data->obReplace)
        || !PySidePropertyBase::assignCheckCallable(removeLast, "removeLast", &data->obRemoveLast)) {
        return -1;
    }

    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::Count, data->obCount != nullptr);
    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::At, data->obAt != nullptr);
    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::Append, data->obAppend != nullptr);
    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::Clear, data->obClear != nullptr);
    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::Replace, data->obReplace != nullptr);
    data->setMethodFlag(QmlListPropertyMixin::MethodFlag::RemoveLast, data->obRemoveLast != nullptr);

    if (notify != nullptr && notify != Py_None)
        data->setNotify(notify);

    data->setDoc(doc != nullptr ? QByteArray(doc) : QByteArray{});

    PyTypeObject *qobjectType = PySide::qObjectType();

    auto *elementType = reinterpret_cast<PyTypeObject *>(data->obElementType);
    PepMroRef elementMro(elementType);
    if (!PySequence_Contains(elementMro.object(), reinterpret_cast<PyObject *>(qobjectType))) {
        PyErr_Format(PyExc_TypeError, "A type inherited from %s expected, got %s.",
                     qobjectType->tp_name, elementType->tp_name);
        return -1;
    }

    data->setTypeName("QQmlListProperty<QObject>"_ba);

    PySide::Property::PropertyFlags flags;
    flags.setFlag(PySide::Property::PropertyFlag::Readable, true);
    flags.setFlag(PySide::Property::PropertyFlag::Designable, designable);
    flags.setFlag(PySide::Property::PropertyFlag::Scriptable, scriptable);
    flags.setFlag(PySide::Property::PropertyFlag::Stored, stored);
    flags.setFlag(PySide::Property::PropertyFlag::User, user);
    flags.setFlag(PySide::Property::PropertyFlag::Constant, constant);
    flags.setFlag(PySide::Property::PropertyFlag::Final, finalProp);
    flags.setFlag(PySide::Property::PropertyFlag::Virtual, virtualProp);
    flags.setFlag(PySide::Property::PropertyFlag::Override, overrideProp);
    data->setFlags(flags);

    data->incref();

    return 0;
}

static int tp_propListTraverse(PyObject *self, visitproc visit, void *arg)
{
    auto *pData = qmlListProperty(self);
    return pData != nullptr ? pData->tp_traverse(visit, arg) : 0;
}

static int tp_propListClear(PyObject *self)
{
    auto *data = reinterpret_cast<PySideProperty *>(self);
    if (data->d == nullptr)
        return 0;

    auto *baseData = std::exchange(data->d, nullptr);
    Q_ASSERT(baseData->type() == PySidePropertyBase::Type::ListProperty);
    static_cast<QmlListPropertyPrivate *>(baseData)->tp_clear();
    delete baseData;
    return 0;
}

static void tp_propListDeAlloc(PyObject *self)
{
    tp_propListClear(self);
    // PYSIDE-939: Handling references correctly.
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(Py_TYPE(self));
    PyObject_GC_UnTrack(self);
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createPropertyListType()
{
    PyType_Slot PropertyListType_slots[] = {
        {Py_tp_new, reinterpret_cast<void *>(propList_tp_new)},
        {Py_tp_init, reinterpret_cast<void *>(propListTpInit)},
        {Py_tp_dealloc, reinterpret_cast<void *>(tp_propListDeAlloc)},
        {Py_tp_traverse, reinterpret_cast<void *>(tp_propListTraverse)},
        {Py_tp_clear, reinterpret_cast<void *>(tp_propListClear)},
        {Py_tp_del, reinterpret_cast<void *>(PyObject_GC_Del)},
        {0, nullptr}
    };

    PyType_Spec PropertyListType_spec = {
        "2:PySide6.QtQml.ListProperty",
        sizeof(PySideProperty),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
        PropertyListType_slots,
    };

    Shiboken::AutoDecRef bases(Py_BuildValue("(O)", PySideProperty_TypeF()));
    return SbkType_FromSpecWithBases(&PropertyListType_spec, bases.object());
}

PyTypeObject *PropertyList_TypeF(void)
{
    // PYSIDE-2230: This was a wrong replacement by static AutoDecref.
    //              Never do that, deletes things way too late.
    static PyTypeObject *type = createPropertyListType();
    return type;
}

} // extern "C"

// Implementation of QQmlListProperty<T>::AppendFunction callback
void QmlListPropertyPrivate::append(QQmlListProperty<QObject> *propList, QObject *item)
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(2));
    PyTypeObject *qobjectType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjectType, propList->object));
    PyTuple_SetItem(args, 1,
                     Shiboken::Conversions::pointerToPython(qobjectType, item));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obAppend, args));

    if (PyErr_Occurred())
        PyErr_Print();
}

// Implementation of QQmlListProperty<T>::CountFunction callback
qsizetype QmlListPropertyPrivate::count(QQmlListProperty<QObject> *propList) const
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(1));
    auto *qobjType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjType, propList->object));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obCount, args));

    // Check return type
    if (PyErr_Occurred()) {
        PyErr_Print();
        return 0;
    }

    qsizetype cppResult = 0;
    auto *converter = Shiboken::Conversions::PrimitiveTypeConverter<qsizetype>();
    if (auto *pythonToCpp = Shiboken::Conversions::isPythonToCppConvertible(converter, retVal))
        pythonToCpp(retVal, &cppResult);
    return cppResult;
}

// Implementation of QQmlListProperty<T>::AtFunction callback
QObject *QmlListPropertyPrivate::at(QQmlListProperty<QObject> *propList, qsizetype index) const
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(2));
    PyTypeObject *qobjectType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjectType, propList->object));
    auto *converter = Shiboken::Conversions::PrimitiveTypeConverter<qsizetype>();
    PyTuple_SetItem(args, 1,
                     Shiboken::Conversions::copyToPython(converter, &index));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obAt, args));

    QObject *result = nullptr;
    auto *elementType = reinterpret_cast<PyTypeObject *>(obElementType);
    if (PyErr_Occurred())
        PyErr_Print();
    else if (PyType_IsSubtype(Py_TYPE(retVal), elementType))
        Shiboken::Conversions::pythonToCppPointer(qobjectType, retVal, static_cast<void*>(&result));
    return result;
}

// Implementation of QQmlListProperty<T>::ClearFunction callback
void QmlListPropertyPrivate::clear(QQmlListProperty<QObject> * propList)
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(1));
    PyTypeObject *qobjectType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjectType, propList->object));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obClear, args));

    if (PyErr_Occurred())
        PyErr_Print();
}

// Implementation of QQmlListProperty<T>::ReplaceFunction callback
void QmlListPropertyPrivate::replace(QQmlListProperty<QObject> *propList, qsizetype index, QObject *value)
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(3));
    PyTypeObject *qobjectType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjectType, propList->object));
    auto *converter = Shiboken::Conversions::PrimitiveTypeConverter<qsizetype>();
    PyTuple_SetItem(args, 1,
                     Shiboken::Conversions::copyToPython(converter, &index));
    PyTuple_SetItem(args, 2,
                     Shiboken::Conversions::pointerToPython(qobjectType, value));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obReplace, args));

    if (PyErr_Occurred())
        PyErr_Print();
}

// Implementation of QQmlListProperty<T>::RemoveLastFunction callback
void QmlListPropertyPrivate::removeLast(QQmlListProperty<QObject> *propList)
{
    Shiboken::GilState state;

    Shiboken::AutoDecRef args(PyTuple_New(1));
    PyTypeObject *qobjectType = PySide::qObjectType();
    PyTuple_SetItem(args, 0,
                     Shiboken::Conversions::pointerToPython(qobjectType, propList->object));

    Shiboken::AutoDecRef retVal(PyObject_CallObject(obRemoveLast, args));

    if (PyErr_Occurred())
        PyErr_Print();
}

static const char *PropertyList_SignatureStrings[] = {
    "PySide6.QtQml.ListProperty(self,type:type,"
        "append:typing.Optional[collections.abc.Callable[...,typing.Any]]=None,"
        "at:typing.Optional[collections.abc.Callable[...,typing.Any]]=None,"
        "clear:typing.Optional[collections.abc.Callable[...,typing.Any]]=None,"
        "count:typing.Optional[collections.abc.Callable[...,typing.Any]]=None)",
    nullptr // Sentinel
};

namespace PySide::Qml {

void initQtQmlListProperty(PyObject *module)
{
    // Export QmlListProperty type
    if (InitSignatureStrings(PropertyList_TypeF(), PropertyList_SignatureStrings) < 0) {
        PyErr_Print();
        qWarning() << "Error initializing PropertyList type.";
        return;
    }

    // Register QQmlListProperty metatype for use in QML
    qRegisterMetaType<QQmlListProperty<QObject>>();

    auto *propertyListType = PropertyList_TypeF();
    auto *obPropertyListType = reinterpret_cast<PyObject *>(propertyListType);
    Py_INCREF(obPropertyListType);
    PepModule_AddType(module, propertyListType);
}

} // namespace PySide::Qml
