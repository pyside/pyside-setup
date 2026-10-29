// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include <sbkpython.h>
#include "pysidesignal.h"
#include "pysidesignal_p.h"
#include "pysideqobject.h"
#include "pysideutils.h"
#include "pysidestaticstrings.h"
#include "qobjectconnect.h"
#include "signalmanager.h"
#include "pysideqenum.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <helper.h>
#include <pep384ext.h>
#include <sbkconverter.h>
#include <sbkenum.h>
#include <sbkerrors.h>
#include <sbkpydictrange.h>
#include <sbkpytuplerange.h>
#include <sbkstaticstrings.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qdebug.h>
#include <QtCore/qhash.h>
#include <QtCore/qobject.h>
#include <QtCore/qmetaobject.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <cstring>

static constexpr char QT_SIGNAL_SENTINEL = '2';

using namespace Qt::StringLiterals;

QDebug operator<<(QDebug debug, const PySideSignalData::Signature &s)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "Signature(\"" << s.signature << '"';
    if (s.attributes)
        debug << ", attributes=" << s.attributes;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const PySideSignalData &d)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "PySideSignalData(\"" << d.signalName << "\", "
          << d.signatures;
    if (!d.signalArguments.isEmpty())
        debug << ", signalArguments=" << d.signalArguments;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const PySideSignalInstancePrivate &d)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "PySideSignalInstancePrivate(\"" << d.signalName
          << "\", \"" << d.signature << '"';
    if (d.attributes)
        debug << ", attributes=" << d.attributes;
    if (d.homonymousMethodPvt)
        debug << ", homonymousMethod=" << d.homonymousMethodPvt;
    debug << ')';
    return debug;
}

static bool isSourceDeleted(const PySideSignalInstance *p)
{
    return p->d == nullptr || p->d->shared->source.isNull();
}

static inline QObject *sender(const PySideSignalInstance *p)
{
    return p->d != nullptr ? p->d->shared->source.data() : nullptr;
}

static inline QByteArray qSignalSignature(const PySideSignalInstance *p)
{
    return QT_SIGNAL_SENTINEL + p->d->signature;
}

static bool connection_Check(PyObject *o)
{
    if (o == nullptr || o == Py_None)
        return false;
    static QByteArray typeName = QByteArrayLiteral("PySide")
        + QByteArray::number(QT_VERSION_MAJOR)
        + QByteArrayLiteral(".QtCore.QMetaObject.Connection");
    return std::strcmp(PepType_GetFullyQualifiedNameStr(Py_TYPE(o)), typeName.constData()) == 0;
}

static std::optional<QByteArrayList> parseArgumentNames(PyObject *argArguments)
{
    QByteArrayList result;
    if (argArguments == nullptr)
        return result;
    // Prevent a string from being split into a sequence of characters
    if (PySequence_Check(argArguments) == 0 || PyUnicode_Check(argArguments) != 0)
        return std::nullopt;
    const Py_ssize_t argumentSize = PySequence_Size(argArguments);
    result.reserve(argumentSize);
    for (Py_ssize_t i = 0; i < argumentSize; ++i) {
        Shiboken::AutoDecRef item(PySequence_GetItem(argArguments, i));
        if (PyUnicode_Check(item.object()) == 0)
            return std::nullopt;
        Shiboken::AutoDecRef strObj(PyUnicode_AsUTF8String(item));
        const char *s = PyBytes_AsString(strObj);
        if (s == nullptr)
            return std::nullopt;
        result.append(QByteArray(s));
    }
    return result;
}

static const char msgSourceDeleted[] = "Signal source has been deleted";
static const char msgTargetSignalDeleted[] = "Target signal has been deleted";

static SbkConverter *metaObjConnectionConverter()
{
    static SbkConverter *result = Shiboken::Conversions::getConverter("QMetaObject::Connection");
    Q_ASSERT(result);
    return result;
}

/*!
    \namespace PySide::Signal
    \brief Namespace for the Signal type.
    \inmodule libpyside
*/
namespace PySide::Signal {
    static QByteArray buildSignature(const QByteArray &, const QByteArray &);
    static void instanceInitialize(PySideSignalInstance *, PyObject *, PySideSignal *,
                                   const PySideSignalInstanceSharedPtr &shared, int);
    static PySideSignalData::Signature parseSignature(PyObject *);
} // PySide::Signal

extern "C"
{

// Signal methods
static int signalTpInit(PyObject *, PyObject *, PyObject *);
static void signalFree(void *);
static void signalInstanceFree(void *);
static PyObject *signalGetItem(PyObject *self, PyObject *key);
static PyObject *signalGetAttr(PyObject *self, PyObject *name);
static PyObject *signalToString(PyObject *self);
static PyObject *signalDescrGet(PyObject *self, PyObject *obj, PyObject *type);

// Signal Instance methods
static PyObject *signalInstanceConnect(PyObject *, PyObject *, PyObject *);
static PyObject *signalInstanceDisconnect(PyObject *, PyObject *);
static PyObject *signalInstanceEmit(PyObject *, PyObject *);
static PyObject *signalInstanceGetItem(PyObject *, PyObject *);

static PyObject *signalInstanceCall(PyObject *self, PyObject *args, PyObject *kw);
static PyObject *signalCall(PyObject *, PyObject *, PyObject *);

static PyObject *metaSignalCheck(PyObject *, PyObject *);


static PyMethodDef MetaSignal_tp_methods[] = {
    {"__instancecheck__", metaSignalCheck, METH_O|METH_STATIC, nullptr},
    {nullptr, nullptr, 0, nullptr}
};

static PyTypeObject *createMetaSignalType()
{
    PyType_Slot PySideMetaSignalType_slots[] = {
        {Py_tp_methods, reinterpret_cast<void *>(MetaSignal_tp_methods)},
        {Py_tp_base,    reinterpret_cast<void *>(&PyType_Type)},
        {Py_tp_free,    reinterpret_cast<void *>(PyObject_GC_Del)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideMetaSignalType_spec = {
        "2:PySide6.QtCore.MetaSignal",
        0,
        // sizeof(PyHeapTypeObject) is filled in by SbkType_FromSpec
        // which calls PyType_Ready which calls inherit_special.
        0,
        Py_TPFLAGS_DEFAULT,
        PySideMetaSignalType_slots,
    };

    return SbkType_FromSpec(&PySideMetaSignalType_spec);
}

static PyTypeObject *PySideMetaSignal_TypeF()
{
    static auto *type = createMetaSignalType();
    return type;
}

static PyTypeObject *createSignalType()
{
    PyType_Slot PySideSignalType_slots[] = {
        {Py_mp_subscript,   reinterpret_cast<void *>(signalGetItem)},
        {Py_tp_getattro,    reinterpret_cast<void *>(signalGetAttr)},
        {Py_tp_descr_get,   reinterpret_cast<void *>(signalDescrGet)},
        {Py_tp_call,        reinterpret_cast<void *>(signalCall)},
        {Py_tp_str,         reinterpret_cast<void *>(signalToString)},
        {Py_tp_init,        reinterpret_cast<void *>(signalTpInit)},
        {Py_tp_new,         reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_free,        reinterpret_cast<void *>(signalFree)},
        {Py_tp_dealloc,     reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideSignalType_spec = {
        "2:PySide6.QtCore.Signal",
        sizeof(PySideSignal),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideSignalType_slots,
    };

    return SbkType_FromSpecWithMeta(&PySideSignalType_spec, PySideMetaSignal_TypeF());
}

PyTypeObject *PySideSignal_TypeF(void)
{
    static auto *type = createSignalType();
    return type;
}

static PyObject *signalInstanceRepr(PyObject *obSelf)
{
    auto *self = reinterpret_cast<PySideSignalInstance *>(obSelf);
    const auto *typeName = Py_TYPE(obSelf)->tp_name;
    return Shiboken::String::fromFormat("<%s %s at %p>", typeName,
                                        self->d ? self->d->signature.constData()
                                                : "(no signature)", obSelf);
}

QT_WARNING_PUSH
QT_WARNING_DISABLE_CLANG("-Wcast-function-type-mismatch")
static PyMethodDef SignalInstance_methods[] = {
    {"connect", reinterpret_cast<PyCFunction>(signalInstanceConnect),
                METH_VARARGS|METH_KEYWORDS, nullptr},
    {"disconnect", signalInstanceDisconnect, METH_VARARGS, nullptr},
    {"emit", signalInstanceEmit, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}  /* Sentinel */
};
QT_WARNING_POP

static PyTypeObject *createSignalInstanceType()
{
    PyType_Slot PySideSignalInstanceType_slots[] = {
        {Py_mp_subscript,   reinterpret_cast<void *>(signalInstanceGetItem)},
        {Py_tp_call,        reinterpret_cast<void *>(signalInstanceCall)},
        {Py_tp_methods,     reinterpret_cast<void *>(SignalInstance_methods)},
        {Py_tp_repr,        reinterpret_cast<void *>(signalInstanceRepr)},
        {Py_tp_new,         reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_free,        reinterpret_cast<void *>(signalInstanceFree)},
        {Py_tp_dealloc,     reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideSignalInstanceType_spec = {
        "2:PySide6.QtCore.SignalInstance",
        sizeof(PySideSignalInstance),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideSignalInstanceType_slots,
    };

    return SbkType_FromSpec(&PySideSignalInstanceType_spec);
}

PyTypeObject *PySideSignalInstance_TypeF(void)
{
    static auto *type = createSignalInstanceType();
    return type;
}

static int signalTpInit(PyObject *obSelf, PyObject *args, PyObject *kwds)
{
    static PyObject * const emptyTuple = PyTuple_New(0);
    static const char *kwlist[] = {"name", "arguments", nullptr};
    char *argName = nullptr;
    PyObject *argArguments = nullptr;

    if (!PyArg_ParseTupleAndKeywords(emptyTuple, kwds,
            "|sO:QtCore.Signal{name, arguments}",
            const_cast<char **>(kwlist), &argName, &argArguments))
        return -1;

    bool tupledArgs = false;
    auto *self = reinterpret_cast<PySideSignal *>(obSelf);
    if (!self->data)
        self->data = new PySideSignalData;
    if (argName)
        self->data->signalName = argName;

    auto argumentNamesOpt = parseArgumentNames(argArguments);
    if (!argumentNamesOpt.has_value()) {
        PyErr_SetString(PyExc_TypeError, "'arguments' must be a sequence of strings.");
        return -1;
    }
    self->data->signalArguments = argumentNamesOpt.value();

    for (auto *arg : Shiboken::PyTupleRange(args)) {
        if (PySequence_Check(arg) && !Shiboken::String::check(arg) && !PyEnumMeta_Check(arg)) {
            tupledArgs = true;
            self->data->signatures.append(PySide::Signal::parseSignature(arg));
        }
    }

    if (!tupledArgs)
        self->data->signatures.append(PySide::Signal::parseSignature(args));

    return 0;
}

static void signalFree(void *vself)
{
    auto *pySelf = reinterpret_cast<PyObject *>(vself);
    auto *self = reinterpret_cast<PySideSignal *>(vself);
    if (self->data) {
        delete self->data;
        self->data = nullptr;
    }
    Py_XDECREF(self->homonymousMethod);
    self->homonymousMethod = nullptr;

    PepExt_TypeCallFree(Py_TYPE(pySelf)->tp_base, self);
}

static PyObject *signalGetItem(PyObject *obSelf, PyObject *key)
{
    auto *self = reinterpret_cast<PySideSignal *>(obSelf);
    QByteArray sigKey;
    if (key) {
        sigKey = PySide::Signal::parseSignature(key).signature;
    } else {
        sigKey = self->data == nullptr || self->data->signatures.isEmpty()
            ? PySide::Signal::voidType() : self->data->signatures.constFirst().signature;
    }
    auto sig = PySide::Signal::buildSignature(self->data->signalName, sigKey);
    return Shiboken::String::fromCString(sig.constData());
}

static PyObject *signalToString(PyObject *obSelf)
{
    auto *self = reinterpret_cast<PySideSignal *>(obSelf);
    QByteArray result;
    if (self->data == nullptr || self->data->signatures.isEmpty()) {
        result = "<invalid>"_ba;
    } else {
        for (const auto &signature : std::as_const(self->data->signatures)) {
            if (!result.isEmpty())
                result += "; "_ba;
            result += PySide::Signal::buildSignature(self->data->signalName,
                                                     signature.signature);
        }
    }
    return Shiboken::String::fromCString(result.constData());
}

static PyObject *signalGetAttr(PyObject *obSelf, PyObject *name)
{
    auto *self = reinterpret_cast<PySideSignal *>(obSelf);

    if (PyUnicode_CompareWithASCIIString(name, "signatures") != 0)
        return PyObject_GenericGetAttr(obSelf, name);

    auto nelems = self->data->signatures.count();
    PyObject *tuple = PyTuple_New(nelems);

    for (Py_ssize_t idx = 0; idx < nelems; ++idx) {
        QByteArray sigKey = self->data->signatures.at(idx).signature;
        auto sig = PySide::Signal::buildSignature(self->data->signalName, sigKey);
        PyObject *entry = Shiboken::String::fromCString(sig.constData());
        PyTuple_SetItem(tuple, idx, entry);
    }
    return tuple;
}

static void signalInstanceFree(void *vself)
{
    auto *pySelf = reinterpret_cast<PyObject *>(vself);
    auto *self = reinterpret_cast<PySideSignalInstance *>(vself);

    PySideSignalInstancePrivate *dataPvt = self->d;
    if (dataPvt) {
        Py_XDECREF(dataPvt->homonymousMethodPvt);

        if (dataPvt->next) {
            Py_DECREF(dataPvt->next);
            dataPvt->next = nullptr;
        }
        delete dataPvt;
        self->d = nullptr;
    }
    PepExt_TypeCallFree(Py_TYPE(pySelf)->tp_base, self);
}

// PYSIDE-1523: PyFunction_Check is not accepting compiled functions and
// PyMethod_Check is not allowing compiled methods, therefore also lookup
// "im_func" and "__code__" attributes, we allow for that with a dedicated
// function handling both.

struct FunctionArgumentsResult
{
    PyObject *function = nullptr;
    PepCodeObject *objCode = nullptr;
    PyObject *functionName = nullptr;
    bool isMethod = false;
};

static FunctionArgumentsResult extractFunctionArgumentsFromSlot(PyObject *slot)
{
    FunctionArgumentsResult ret;
    ret.isMethod = PyMethod_Check(slot);
    const bool isFunction = PyFunction_Check(slot);

    if (ret.isMethod || isFunction) {
        ret.function = ret.isMethod ? PyMethod_GET_FUNCTION(slot) : slot;
        ret.objCode = reinterpret_cast<PepCodeObject *>(PyFunction_GET_CODE(ret.function));
        ret.functionName = PepFunction_GetName(ret.function);

    } else if (Shiboken::isCompiledMethod(slot)) {
        // PYSIDE-1523: PyFunction_Check and PyMethod_Check are not accepting compiled forms, we
        // just go by attributes.
        ret.isMethod = true;

        ret.function = PyObject_GetAttr(slot, PySide::PySideName::im_func());
        // Not retaining a reference inline with what PyMethod_GET_FUNCTION does.
        Py_DECREF(ret.function);

        ret.functionName = PyObject_GetAttr(ret.function, PySide::PySideMagicName::name());
        // Not retaining a reference inline with what PepFunction_GetName does.
        Py_DECREF(ret.functionName);

        auto *obObjCode = PyObject_GetAttr(ret.function, PySide::PySideMagicName::code());
        ret.objCode = reinterpret_cast<PepCodeObject *>(obObjCode);
        // Not retaining a reference inline with what PyFunction_GET_CODE does.
        Py_XDECREF(obObjCode);

        // Should not happen, but lets handle it gracefully, maybe Nuitka one day
        // makes these optional, or somebody defined a type named like it without
        // it being actually being that.
        if (ret.objCode == nullptr)
            ret.function = nullptr;
    } else if (std::strcmp(PepType_GetFullyQualifiedNameStr(Py_TYPE(slot)), "compiled_function") == 0) {
        ret.isMethod = false;
        ret.function = slot;

        ret.functionName = PyObject_GetAttr(ret.function, PySide::PySideMagicName::name());
        // Not retaining a reference inline with what PepFunction_GetName does.
        Py_DECREF(ret.functionName);

        auto *obObjCode = PyObject_GetAttr(ret.function, PySide::PySideMagicName::code());
        ret.objCode = reinterpret_cast<PepCodeObject *>(obObjCode);
        // Not retaining a reference inline with what PyFunction_GET_CODE does.
        Py_XDECREF(obObjCode);

        // Should not happen, but lets handle it gracefully, maybe Nuitka one day
        // makes these optional, or somebody defined a type named like it without
        // it being actually being that.
        if (ret.objCode == nullptr)
            ret.function = nullptr;
    }
    // any other callback
    return ret;
}

struct ArgCount
{
    int min;
    int max;
};

// Return a pair of minimum / arg count "foo(p1, p2=0)" -> {1, 2}
ArgCount argCount(const FunctionArgumentsResult &args)
{
    Q_ASSERT(args.objCode);
    ArgCount result{-1, -1};
    if ((PepCode_GET_FLAGS(args.objCode) & CO_VARARGS) == 0) {
        result.min = result.max = PepCode_GET_ARGCOUNT(args.objCode);
        if (args.function != nullptr) {
            if (auto *defaultArgs = PepFunction_GetDefaults(args.function))
                result.min -= PyTuple_Size(defaultArgs);
        }
    }
    return result;
}

// Find Signal Instance for argument count.
static PySideSignalInstance *findSignalInstance(PySideSignalInstance *source, int argCount)
{
    for (auto *si = source; si != nullptr; si = si->d->next) {
        if (si->d->argCount == argCount)
            return si;
    }
    return nullptr;
}

static PySideSignalInstance *findSignalInstanceForSlot(PySideSignalInstance *source,
                                                       PyObject *slot)
{
    Q_ASSERT(slot != nullptr && slot != Py_None);

    // Check signature of the slot (method or function) to match signal
    const auto args = extractFunctionArgumentsFromSlot(slot);

    if (args.function != nullptr && source->d->next != nullptr) {
        auto slotArgRange = argCount(args);
        if (args.isMethod) {
            slotArgRange.min -= 1;
            slotArgRange.max -= 1;
        }

        // Get signature args
        // Iterate the possible types of connection for this signal and compare
        // it with slot arguments
        for (int slotArgs = slotArgRange.max; slotArgs >= slotArgRange.min; --slotArgs) {
            if (auto *matchedSlot = findSignalInstance(source, slotArgs))
                return matchedSlot;
        }
    }
    return source;
}

static PyObject *connectSignalToSignal(PySideSignalInstance *source, PySideSignalInstance *target,
                                       Qt::ConnectionType connectionType)
{
    // find best match
    for (PySideSignalInstance *sourceWalk = source; sourceWalk != nullptr; ) {
        for (auto *targetWalk = target; targetWalk != nullptr; ) {
            const char *sourceSignature = sourceWalk->d->signature.constData();
            const char *targetSignature = targetWalk->d->signature.constData();
            if (QMetaObject::checkConnectArgs(sourceSignature, targetSignature)) {
                if (isSourceDeleted(sourceWalk))
                    return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
                if (isSourceDeleted(targetWalk))
                    return PyErr_Format(PyExc_RuntimeError, msgTargetSignalDeleted);

                auto *senderObj = sender(sourceWalk);
                const auto *senderMo = senderObj->metaObject();
                const int senderIndex = senderMo->indexOfSignal(sourceSignature);
                auto *receiver = sender(targetWalk);
                const auto *receiverMo = receiver->metaObject();
                const int receiverIndex = receiverMo->indexOfSignal(targetSignature);
                if (senderIndex == -1 || receiverIndex == -1)
                    break; // Should not happen
                auto conn = QObject::connect(senderObj, senderMo->method(senderIndex),
                                             receiver, receiverMo->method(receiverIndex),
                                             connectionType);
                return Shiboken::Conversions::copyToPython(metaObjConnectionConverter(), &conn);
            }
            targetWalk = targetWalk->d->next;
        }
        sourceWalk = sourceWalk->d->next;
    }
    return PyErr_Format(PyExc_RuntimeError, R"(Failed to connect signal "%s" to signal "%s".)",
                        source->d->signature.constData(), target->d->signature.constData());
}

static PyObject *signalInstanceConnect(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *slot = nullptr;
    PyObject *type = nullptr;
    static const char *kwlist[] = {"slot", "type", nullptr};

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
        "O|O:SignalInstance", const_cast<char **>(kwlist), &slot, &type))
        return nullptr;

    Qt::ConnectionType connectionType = Qt::AutoConnection;
    // Compare the plain type name: the qualified one carries the enclosing
    // enum scope ("Qt.ConnectionType"), so matching it whole silently dropped
    // the requested connection type and fell back to Qt::AutoConnection.
    if (type != nullptr
        && qstrcmp(PepType_GetNameStr(Py_TYPE(type)), "ConnectionType") == 0) {
        static SbkConverter *connectionTypeConv =
            Shiboken::Conversions::getConverter("Qt::ConnectionType");
        Q_ASSERT(connectionTypeConv);
        Shiboken::Conversions::pythonToCppCopy(connectionTypeConv, type, &connectionType);
    }

    auto *source = reinterpret_cast<PySideSignalInstance *>(self);
    if (!source->d)
        return PyErr_Format(PyExc_RuntimeError, "cannot connect uninitialized SignalInstance");

    if (Py_TYPE(slot) == PySideSignalInstance_TypeF()) // Connect signal to signal
        return connectSignalToSignal(source, reinterpret_cast<PySideSignalInstance *>(slot), connectionType);

    if (PyCallable_Check(slot) == 0) {
        return PyErr_Format(PyExc_TypeError, "Expected signal or callable, got \"%s\"",
                            Py_TYPE(slot)->tp_name);
    }
    if (isSourceDeleted(source))
        return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
    PySideSignalInstance *matchedSlot = findSignalInstanceForSlot(source, slot);
    auto conn = PySide::qobjectConnectCallback(sender(source),
                                               qSignalSignature(matchedSlot).constData(),
                                               slot, connectionType);
    return Shiboken::Conversions::copyToPython(metaObjConnectionConverter(), &conn);
}

static inline void initPySideSignalInstancePrivate(PySideSignalInstancePrivate *p)
{
    if (p->signalIndex == -1 && !p->shared->source.isNull())
        p->signalIndex = p->shared->source->metaObject()->indexOfSignal(p->signature);
}

static PyObject *signalInstanceEmit(PyObject *self, PyObject *args)
{
    auto *source = reinterpret_cast<PySideSignalInstance *>(self);
    if (!source->d)
        return PyErr_Format(PyExc_RuntimeError, "cannot emit uninitialized SignalInstance");

    if (isSourceDeleted(source))
        return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);

    Shiboken::AutoDecRef pyArgs(PyList_New(0));
    const Py_ssize_t numArgsGiven = PySequence_Size(args);
    initPySideSignalInstancePrivate(source->d);

    // If number of arguments given to emit is smaller than the first source signature expects,
    // it is possible it's a case of emitting a signal with default parameters.
    // Search through all the overloaded signals with the same name, and try to find a signature
    // with the same number of arguments as given to emit, and is also marked as a cloned method
    // (which in metaobject parlance means a signal with default parameters).
    // @TODO: This should be improved to take into account argument types as well. The current
    // assumption is there are no signals which are both overloaded on argument types and happen to
    // have signatures with default parameters.
    if (numArgsGiven < source->d->argCount) {
        PySideSignalInstance *possibleDefaultInstance = source;
        while ((possibleDefaultInstance = possibleDefaultInstance->d->next)) {
            initPySideSignalInstancePrivate(possibleDefaultInstance->d);
            if (possibleDefaultInstance->d->attributes & QMetaMethod::Cloned
                    && possibleDefaultInstance->d->argCount == numArgsGiven) {
                source = possibleDefaultInstance;
                break;
            }
        }
    }

    const bool ok = PySide::SignalManager::emitSignal(sender(source), source->d->signalIndex, args);
    if (PyErr_Occurred() != nullptr)
        return nullptr;
    if (ok)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyObject *signalInstanceGetItem(PyObject *self, PyObject *key)
{
    auto *firstSignal = reinterpret_cast<PySideSignalInstance *>(self);
    const auto &sigName = firstSignal->d->signalName;
    const auto sigKey = PySide::Signal::parseSignature(key).signature;
    const auto sig = PySide::Signal::buildSignature(sigName, sigKey);
    for (auto *data = firstSignal; data != nullptr; data = data->d->next) {
        if (data->d->signature == sig) {
            auto *result = reinterpret_cast<PyObject *>(data);
            Py_INCREF(result);
            return result;
        }
    }

    // Build error message with candidates
    QByteArray message = "Signature \"" + sig + "\" not found for signal: \""
                         + sigName + "\". Available candidates: ";
    for (auto *data = firstSignal; data != nullptr; data = data->d->next) {
        if (data != firstSignal)
            message += ", ";
        message += '"' + data->d->signature + '"';
    }

    return PyErr_Format(PyExc_IndexError, message.constData());
}

static inline void warnDisconnectFailed(PyObject *aSlot, const QByteArray &signature)
{
    if (PyErr_Occurred() != nullptr) { // avoid "%S" invoking str() when an error is set.
        Shiboken::Errors::Stash errorStash;
        PyErr_WarnFormat(PyExc_RuntimeWarning, 0,
                         "libpyside: Failed to disconnect (%s) from signal \"%s\".",
                         Py_TYPE(aSlot)->tp_name, signature.constData());
    } else {
        PyErr_WarnFormat(PyExc_RuntimeWarning, 0,
                         "libpyside: Failed to disconnect (%S) from signal \"%s\".",
                         aSlot, signature.constData());
    }
}

static PyObject *signalInstanceDisconnect(PyObject *self, PyObject *args)
{
    auto *source = reinterpret_cast<PySideSignalInstance *>(self);
    if (!source->d)
        return PyErr_Format(PyExc_RuntimeError, "cannot disconnect uninitialized SignalInstance");

    Shiboken::AutoDecRef pyArgs(PyList_New(0));

    PyObject *slot = Py_None;
    if (PyTuple_Check(args) && PyTuple_Size(args))
        slot = PyTuple_GetItem(args, 0);

    if (Py_TYPE(slot) == PySideSignalInstance_TypeF()) { // Disconnect signal from signal
        auto *target = reinterpret_cast<PySideSignalInstance *>(slot);
        if (!QMetaObject::checkConnectArgs(source->d->signature, target->d->signature)) {
            warnDisconnectFailed(slot, source->d->signature);
            Py_RETURN_FALSE;
        }

        if (isSourceDeleted(source))
            return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
        if (isSourceDeleted(target))
            return PyErr_Format(PyExc_RuntimeError, msgTargetSignalDeleted);

        auto *qSender = sender(source);
        const bool ok = qSender->disconnect(qSignalSignature(source).constData(),
                                            sender(target),
                                            qSignalSignature(target).constData());
        if (ok)
            Py_RETURN_TRUE;
        warnDisconnectFailed(slot, source->d->signature);
        Py_RETURN_FALSE;
    }

    if (slot == Py_None) { // disconnect all signal receivers by signature
        if (isSourceDeleted(source))
            return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
        const bool ok =
            source->d->shared->source->disconnect(qSignalSignature(source).constData());
        if (ok)
            Py_RETURN_TRUE;
        warnDisconnectFailed(slot, source->d->signature);
        Py_RETURN_FALSE;
    }

    if (connection_Check(slot)) { // disconnection by connection ID
        QMetaObject::Connection conn;
        Shiboken::Conversions::pythonToCppCopy(metaObjConnectionConverter(), slot, &conn);
        if (conn && QObject::disconnect(conn))
            Py_RETURN_TRUE;
        warnDisconnectFailed(slot, source->d->signature);
        Py_RETURN_FALSE;
    }

    // Disconnect callable
    if (PyCallable_Check(slot) == 0) {
        return PyErr_Format(PyExc_TypeError,
                            "Expected signal, callable or connection id, got \"%s\"",
                            Py_TYPE(slot)->tp_name);
    }

    // try the matching signature, fall back to first
    auto *matchedSlot = slot != Py_None ? findSignalInstanceForSlot(source, slot) : source;
    if (isSourceDeleted(matchedSlot))
        return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);

    const auto sourceSignature = qSignalSignature(matchedSlot);
    // disconnect all, so we need to use the c++ signature disconnect(qobj, signal, 0, 0)
    auto *qSender = sender(matchedSlot);
    if (slot == Py_None) {
        const bool ok = qSender->disconnect(sourceSignature.constData(), nullptr, nullptr);
        if (ok)
            Py_RETURN_TRUE;
        warnDisconnectFailed(slot, source->d->signature);
        Py_RETURN_FALSE;
    }

    const bool ok = PySide::qobjectDisconnectCallback(qSender, sourceSignature, slot);
    if (ok)
        Py_RETURN_TRUE;
    warnDisconnectFailed(slot, source->d->signature);
    Py_RETURN_FALSE;
}

// PYSIDE-68: Supply the missing __get__ function
static PyObject *signalDescrGet(PyObject *self, PyObject *obj, PyObject * /*type*/)
{
    auto *signal = reinterpret_cast<PySideSignal *>(self);
    // Return the unbound signal if there is nothing to bind it to.
    if (obj == nullptr || obj == Py_None
        || !PySide::isQObjectDerived(Py_TYPE(obj), true)) {
        Py_INCREF(self);
        return self;
    }

    // Signals are named and registered when the type is parsed. An empty name
    // means this Signal was attached to the type afterwards, so it never
    // reached the meta object. Binding it anyway used to build an instance
    // with the signature "()", which Qt then connects by index without an
    // argument check - emitting it reads arguments that were never written.
    if (signal->data->signalName.isEmpty()) {
        return PyErr_Format(PyExc_RuntimeError,
                            "Signal object of type '%.200s' was added after the class was created "
                            "and is therefore not registered. Declare signals in the class body.",
                            Py_TYPE(obj)->tp_name);
    }

    // PYSIDE-68-bis: It is important to respect the already cached instance.
    Shiboken::AutoDecRef name(Py_BuildValue("s", signal->data->signalName.data()));
    auto *dict = SbkObject_GetDict_NoRef(obj);
    auto *inst = PyDict_GetItem(dict, name);
    if (inst) {
        Py_INCREF(inst);
        return inst;
    }
    inst = reinterpret_cast<PyObject *>(PySide::Signal::initialize(signal, name, obj));
    PyObject_SetAttr(obj, name, inst);
    return inst;
}

static PyObject *signalCall(PyObject *self, PyObject *args, PyObject *kw)
{
    auto *signal = reinterpret_cast<PySideSignal *>(self);

    // Native C++ signals can't be called like functions, thus we throw an exception.
    // The only way calling a signal can succeed (the Python equivalent of C++'s operator() )
    // is when a method with the same name as the signal is attached to an object.
    // An example is QProcess::error() (don't check the docs, but the source code of qprocess.h).
    if (!signal->homonymousMethod)
        return PyErr_Format(PyExc_TypeError, "native Qt signal is not callable");

    // Check if there exists a method with the same name as the signal, which is also a static
    // method in C++ land.
    Shiboken::AutoDecRef homonymousMethod(PepExt_Type_CallDescrGet(signal->homonymousMethod,
                                                                   nullptr, nullptr));
    if (PyCFunction_Check(homonymousMethod.object())
            && (PyCFunction_GetFlags(homonymousMethod.object()) & METH_STATIC))
        return PyObject_Call(homonymousMethod, args, kw);

    // Assumes homonymousMethod is not a static method.
    ternaryfunc callFunc = PepExt_Type_GetCallSlot(Py_TYPE(signal->homonymousMethod));
    return callFunc(homonymousMethod, args, kw);
}

// This function returns a borrowed reference.
static inline PyObject *_getRealCallable(PyObject *func)
{
    static const auto *SignalType = PySideSignal_TypeF();
    static const auto *SignalInstanceType = PySideSignalInstance_TypeF();

    // If it is a signal, use the (maybe empty) homonymous method.
    if (Py_TYPE(func) == SignalType) {
        auto *signal = reinterpret_cast<PySideSignal *>(func);
        return signal->homonymousMethod;
    }
    // If it is a signal instance, use the (maybe empty) homonymous method.
    if (Py_TYPE(func) == SignalInstanceType) {
        auto *signalInstance = reinterpret_cast<PySideSignalInstance *>(func);
        return signalInstance->d->homonymousMethodPvt;
    }
    return func;
}

// This function returns a borrowed reference.
static PyObject *_getHomonymousMethod(PySideSignalInstance *inst)
{
    if (inst->d->homonymousMethodPvt)
        return inst->d->homonymousMethodPvt;

    // PYSIDE-1730: We are searching methods with the same name not only at the same place,
    // but walk through the whole mro to find a hidden method with the same name.
    auto signalName = inst->d->signalName;
    Shiboken::AutoDecRef name(Shiboken::String::fromCString(signalName));

    auto *sourceType = inst->d->shared->sourceType;
    for (PyTypeObject *sub_type : Shiboken::PyTupleTypeRange(sourceType->tp_mro)) {
        Shiboken::AutoDecRef tpDict(PepType_GetDict(sub_type));
        auto *hom = PyDict_GetItem(tpDict, name);
        if (hom != nullptr && PyCallable_Check(hom) != 0) {
            if (auto *realFunc = _getRealCallable(hom))
                return realFunc;
        }
    }
    return nullptr;
}

static PyObject *signalInstanceCall(PyObject *self, PyObject *args, PyObject *kw)
{
    auto *PySideSignal = reinterpret_cast<PySideSignalInstance *>(self);
    if (isSourceDeleted(PySideSignal))
        return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
    auto *hom = _getHomonymousMethod(PySideSignal);
    if (!hom) {
        PyErr_Format(PyExc_TypeError, "native Qt signal instance '%s' is not callable",
                     PySideSignal->d->signalName.constData());
        return nullptr;
    }

    auto *source = PySide::getWrapperForQObject(sender(PySideSignal),
                                                PySideSignal->d->shared->sourceType);
    if (source == nullptr)
        return PyErr_Format(PyExc_RuntimeError, msgSourceDeleted);
    Shiboken::AutoDecRef homonymousMethod(PepExt_Type_CallDescrGet(hom, source, nullptr));
    return PyObject_Call(homonymousMethod, args, kw);
}

static PyObject *metaSignalCheck(PyObject * /* klass */, PyObject *arg)
{
    if (PyType_IsSubtype(Py_TYPE(arg), PySideSignalInstance_TypeF()))
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

} // extern "C"

namespace PySide::Signal {

static const char *MetaSignal_SignatureStrings[] = {
    "PySide6.QtCore.MetaSignal.__instancecheck__(self,object:object)->bool",
    nullptr}; // Sentinel

static const char *Signal_SignatureStrings[] = {
    "PySide6.QtCore.Signal(self,*types:type,name:str=nullptr,arguments:typing.List[str]=nullptr)",
    "1:PySide6.QtCore.Signal.__get__(self,instance:None,owner:Optional[typing.Any])->"
        "PySide6.QtCore.Signal",
    "0:PySide6.QtCore.Signal.__get__(self,instance:PySide6.QtCore.QObject,"
        "owner:Optional[typing.Any])->PySide6.QtCore.SignalInstance",
    nullptr}; // Sentinel

static const char *SignalInstance_SignatureStrings[] = {
    "PySide6.QtCore.SignalInstance.connect(self,slot:object,"
        "type:PySide6.QtCore.Qt.ConnectionType=PySide6.QtCore.Qt.ConnectionType.AutoConnection)"
        "->PySide6.QtCore.QMetaObject.Connection",
    "PySide6.QtCore.SignalInstance.disconnect(self,slot:object=nullptr)->bool",
    "PySide6.QtCore.SignalInstance.emit(self,*args:typing.Any)",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *metaSignalType = PySideMetaSignal_TypeF();
    if (InitSignatureStrings(metaSignalType, MetaSignal_SignatureStrings) < 0)
        return;
    auto *obMetaSignalType = reinterpret_cast<PyObject *>(metaSignalType);
    Py_INCREF(obMetaSignalType);
    PepModule_AddType(module, metaSignalType);

    auto *signalType = PySideSignal_TypeF();
    if (InitSignatureStrings(signalType, Signal_SignatureStrings) < 0)
        return;
    auto *obSignalType = reinterpret_cast<PyObject *>(signalType);
    Py_INCREF(obSignalType);
    PepModule_AddType(module, signalType);

    auto *signalInstanceType = PySideSignalInstance_TypeF();
    if (InitSignatureStrings(signalInstanceType, SignalInstance_SignatureStrings) < 0)
        return;
    auto *obSignalInstanceType = reinterpret_cast<PyObject *>(signalInstanceType);
    Py_INCREF(obSignalInstanceType);
    PepModule_AddType(module, signalInstanceType);
}

bool checkType(PyObject *pyObj)
{
    if (pyObj)
        return PyType_IsSubtype(Py_TYPE(pyObj), PySideSignal_TypeF());
    return false;
}

bool checkInstanceType(PyObject *pyObj)
{
    return pyObj != nullptr
        && PyType_IsSubtype(Py_TYPE(pyObj), PySideSignalInstance_TypeF()) != 0;
}

void updateSourceObject(PyObject *source)
{
    // TODO: Provide for actual upstream exception handling.
    //       For now we'll just return early to avoid further issues.

    if (source == nullptr)      // Bad input
       return;

    Shiboken::AutoDecRef mroIterator(PyObject_GetIter(source->ob_type->tp_mro));

    if (mroIterator.isNull())   // Not iterable
       return;

    Shiboken::AutoDecRef mroItem{};
    auto *dict = SbkObject_GetDict_NoRef(source);

    // PYSIDE-1431: Walk the mro and update. But see PYSIDE-1751 below.
    while ((mroItem.reset(PyIter_Next(mroIterator))), mroItem.object()) {
        auto *type = reinterpret_cast<PyTypeObject *>(mroItem.object());
        Shiboken::AutoDecRef tpDict(PepType_GetDict(type));
        for (auto [key, value] : Shiboken::PyDictRange(tpDict.object())) {
            if (PyObject_TypeCheck(value, PySideSignal_TypeF())) {
                // PYSIDE-1751: We only insert an instance into the instance dict, if a signal
                //              of the same name is in the mro. This is the equivalent action
                //              as PyObject_SetAttr, but filtered by existing signal names.
                if (!PyDict_GetItem(dict, key)) {
                    auto *inst = PyObject_New(PySideSignalInstance, PySideSignalInstance_TypeF());
                    Shiboken::AutoDecRef signalInstance(reinterpret_cast<PyObject *>(inst));
                    auto *si = reinterpret_cast<PySideSignalInstance *>(signalInstance.object());
                    auto shared = std::make_shared<PySideSignalInstanceShared>();
                    shared->source = PySide::convertToQObject(source, false);
                    shared->sourceType = Py_TYPE(source);
                    instanceInitialize(si, key, reinterpret_cast<PySideSignal *>(value),
                                       shared, 0);
                    if (PyDict_SetItem(dict, key, signalInstance) == -1)
                        return;     // An error occurred while setting the attribute
                }
            }
        }
    }

    if (PyErr_Occurred())       // An iteration error occurred
        return;
}

QByteArray getTypeName(PyObject *obType)
{
    if (PyType_Check(obType)) {
        auto *type = reinterpret_cast<PyTypeObject *>(obType);
        if (PyType_IsSubtype(type, SbkObject_TypeF()))
            return Shiboken::ObjectType::getOriginalName(type);
        // Translate Python types to Qt names
        if (Shiboken::String::checkType(type))
            return QByteArrayLiteral("QString");
        if (type == &PyLong_Type)
            return QByteArrayLiteral("int");
        if (type == &PyFloat_Type)
            return QByteArrayLiteral("double");
        if (type == &PyBool_Type)
            return QByteArrayLiteral("bool");
        if (type == &PyList_Type)
            return QByteArrayLiteral("QVariantList");
        if (type == &PyDict_Type)
            return QByteArrayLiteral("QVariantMap");
        QByteArray enumName = PySide::QEnum::getTypeName(type);
        return enumName.isEmpty() ? "PyObject"_ba : enumName;
    }
    if (obType == Py_None) // Must be checked before as Shiboken::String::check accepts Py_None
        return voidType();
    if (Shiboken::String::check(obType)) {
        QByteArray result = Shiboken::String::toCString(obType);
        if (result == "qreal")
            result = sizeof(qreal) == sizeof(double) ? "double" : "float";
        return result;
    }
    return {};
}

static QByteArray buildSignature(const QByteArray &name, const QByteArray &signature)
{
    return QMetaObject::normalizedSignature(name + '(' + signature + ')');
}

static void warnInvalidSignature(PyObject *args, Py_ssize_t argNumber)
{
    PyErr_WarnFormat(PyExc_RuntimeWarning, 0,
                     R"(libpyside: Ignoring invalid signal parameter %zd ("%S"/%T).)",
                     argNumber + 1, args, args);
}

static PySideSignalData::Signature parseSignature(PyObject *args)
{
    PySideSignalData::Signature result{{}, QMetaMethod::Compatibility, 0};
    if (args && (Shiboken::String::check(args) || !PyTuple_Check(args))) {
        result.signature = getTypeName(args);
        if (result.signature.isEmpty()) // FIXME PYSIDE7: Turn into error (PYSIDE-3364)?
            warnInvalidSignature(args, 0);
        result.argCount = 1;
        return result;
    }

    for (Py_ssize_t i = 0, i_max = PySequence_Size(args); i < i_max; i++) {
        Shiboken::AutoDecRef arg(PySequence_GetItem(args, i));
        const auto typeName = getTypeName(arg);
        if (typeName.isEmpty()) {
            warnInvalidSignature(args, i); // FIXME PYSIDE7: Turn into error (PYSIDE-3364)?
        } else {
            if (!result.signature.isEmpty())
                result.signature += ',';
            result.signature += typeName;
            ++result.argCount;
        }
    }
    return result;
}

static void instanceInitialize(PySideSignalInstance *self, PyObject *name,
                               PySideSignal *signal,
                               const PySideSignalInstanceSharedPtr &shared,
                               int index)
{
    self->d = new PySideSignalInstancePrivate;
    PySideSignalInstancePrivate *selfPvt = self->d;
    selfPvt->shared = shared;
    selfPvt->next = nullptr;
    if (signal->data->signalName.isEmpty())
        signal->data->signalName = Shiboken::String::toCString(name);
    selfPvt->signalName = signal->data->signalName;

    const auto &signature = signal->data->signatures.at(index);
    selfPvt->signature = buildSignature(self->d->signalName, signature.signature);
    selfPvt->argCount = signature.argCount;
    selfPvt->attributes = signature.attributes;
    selfPvt->homonymousMethodPvt = nullptr;
    if (signal->homonymousMethod) {
        selfPvt->homonymousMethodPvt = signal->homonymousMethod;
        Py_INCREF(selfPvt->homonymousMethodPvt);
    }

    index++;

    if (index < signal->data->signatures.size()) {
        selfPvt->next = PyObject_New(PySideSignalInstance, PySideSignalInstance_TypeF());
        instanceInitialize(selfPvt->next, name, signal, shared, index);
    }
}

/*!
    Initializes the Signal object by creating a PySideSignalInstance.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a self
            \li A Signal object used as base for the PySideSignalInstance.
        \row
            \li \a name
            \li The name to be used for the PySideSignalInstance.
        \row
            \li \a object
            \li The PyObject to which the signal will be attached.
    \endtable

    Returns a new reference to the PySideSignalInstance.
*/
PySideSignalInstance *initialize(PySideSignal *self, PyObject *name, PyObject *object)
{
    PyTypeObject *pyQObjectType = PySide::qObjectType();
    assert(pyQObjectType);

    if (!PyObject_TypeCheck(object, pyQObjectType)) {
        PyErr_Format(PyExc_TypeError, "%s cannot be converted to %s",
                                      Py_TYPE(object)->tp_name, pyQObjectType->tp_name);
        return nullptr;
    }

    PySideSignalInstance *instance = PyObject_New(PySideSignalInstance,
                                                  PySideSignalInstance_TypeF());
    auto shared = std::make_shared<PySideSignalInstanceShared>();
    shared->source = PySide::convertToQObject(object, false);
    shared->sourceType = Py_TYPE(object);
    instanceInitialize(instance, name, self, shared, 0);
    return instance;
}

bool connect(PyObject *source, const char *signal, PyObject *callback)
{
    Shiboken::AutoDecRef pyMethod(PyObject_GetAttr(source,
                                                   PySide::PySideName::qtConnect()));
    if (pyMethod.isNull())
        return false;

    Shiboken::AutoDecRef pySignature(Shiboken::String::fromCString(signal));
    Shiboken::AutoDecRef pyArgs(PyTuple_Pack(3, source, pySignature.object(), callback));
    PyObject *result =  PyObject_CallObject(pyMethod, pyArgs);
    if (result == Py_False) {
        PyErr_Format(PyExc_RuntimeError, "Failed to connect signal %s, to python callable object.", signal);
        Py_DECREF(result);
        result = nullptr;
    }
    return result;
}

/*!
    Creates a Signal object which stays attached to a QObject class based
    on a list of QMetaMethods.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a sourceQObject
            \li The QObject emitting the signal.
        \row
            \li \a source
            \li The Python object of \a sourceQObject on which the signal
                is registered.
        \row
            \li \a methods
            \li A list of QMetaMethod containing the supported signatures.
    \endtable

    Returns a new reference to a PySideSignalInstance.
*/
PySideSignalInstance *newObjectFromMethod(QObject *sourceQObject, PyObject *source,
                                          const QList<QMetaMethod>& methods)
{
    PySideSignalInstance *root = nullptr;
    PySideSignalInstance *previous = nullptr;
    auto shared = std::make_shared<PySideSignalInstanceShared>();
    shared->source = sourceQObject;
    shared->sourceType = Py_TYPE(source);
    for (const QMetaMethod &m : methods) {
        PySideSignalInstance *item = PyObject_New(PySideSignalInstance, PySideSignalInstance_TypeF());
        if (!root)
            root = item;

        if (previous)
            previous->d->next = item;

        item->d = new PySideSignalInstancePrivate;
        PySideSignalInstancePrivate *selfPvt = item->d;
        selfPvt->shared = shared;
        // separate SignalName
        selfPvt->signalName = m.name();
        selfPvt->signature = m.methodSignature();
        selfPvt->argCount = short(m.parameterCount());
        selfPvt->attributes = m.attributes();
        selfPvt->homonymousMethodPvt = nullptr;
        selfPvt->next = nullptr;
    }
    return root;
}

static void _addSignalToWrapper(PyTypeObject *wrapperType, const char *signalName, PySideSignal *signal)
{
    Shiboken::AutoDecRef tpDict(PepType_GetDict(wrapperType));
    auto *typeDict = tpDict.object();
    if (auto *homonymousMethod = PyDict_GetItemString(typeDict, signalName)) {
        Py_INCREF(homonymousMethod);
        signal->homonymousMethod = homonymousMethod;
    }
    PyDict_SetItemString(typeDict, signalName, reinterpret_cast<PyObject *>(signal));
}

// This function is used by qStableSort to promote empty signatures
static bool compareSignals(const PySideSignalData::Signature &sig1,
                           const PySideSignalData::Signature &sig2)
{
    return sig1.signature.isEmpty() && !sig2.signature.isEmpty();
}

void registerSignals(PyTypeObject *pyObj, const QMetaObject *metaObject)
{
    using Signature = PySideSignalData::Signature;
    struct MetaSignal
    {
        QByteArray methodName;
        QList<Signature> signatures;
    };

    QList<MetaSignal> signalsFound;
    for (int i = metaObject->methodOffset(), max = metaObject->methodCount(); i < max; ++i) {
        QMetaMethod method = metaObject->method(i);

        if (method.methodType() == QMetaMethod::Signal) {
            const QByteArray &methodName = method.name();
            Signature signature{method.parameterTypes().join(','), {},
                                short(method.parameterCount())};
            if (method.attributes() & QMetaMethod::Cloned)
                signature.attributes = QMetaMethod::Cloned;
            auto it = std::find_if(signalsFound.begin(), signalsFound.end(),
                                   [methodName](const MetaSignal &ms)
                                   { return ms.methodName == methodName; });
            if (it != signalsFound.end())
                it->signatures << signature;
            else
                signalsFound.append(MetaSignal{methodName, {signature}});
        }
    }

    for (const auto &metaSignal : std::as_const(signalsFound)) {
        PySideSignal *self = PyObject_New(PySideSignal, PySideSignal_TypeF());
        self->data = new PySideSignalData;
        self->data->signalName = metaSignal.methodName;
        self->homonymousMethod = nullptr;

        // Empty signatures comes first! So they will be the default signal signature
        self->data->signatures = metaSignal.signatures;
        std::stable_sort(self->data->signatures.begin(),
                         self->data->signatures.end(), &compareSignals);

        _addSignalToWrapper(pyObj, metaSignal.methodName, self);
        Py_DECREF(reinterpret_cast<PyObject *>(self));
    }
}

PyObject *getObject(PySideSignalInstance *signal)
{
    if (auto *qSender = sender(signal))
        return getWrapperForQObject(qSender, signal->d->shared->sourceType);
    return nullptr;
}

const char *getSignature(PySideSignalInstance *signal)
{
    return signal->d->signature;
}

EmitterData getEmitterData(PySideSignalInstance *signal)
{
    EmitterData result;
    result.emitter = PySide::convertToQObject(getObject(signal), false);
    if (result.emitter != nullptr) {
        const auto *mo = result.emitter->metaObject();
        result.methodIndex = mo->indexOfMethod(getSignature(signal));
    }
    return result;
}

/*!
    Returns the signature of a callback based on the signal and the receiver
    callback.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a signal
            \li The signal.
        \row
            \li \a receiver
            \li The QObject which will receive the signal.
        \row
            \li \a callback
            \li Callback function which will be connected to the signal.
        \row
            \li \a encodeName
            \li Specifies whether the returned signature will be encoded in
                Qt signal/slot style.
    \endtable
*/
QByteArray getCallbackSignature(QMetaMethod signal, QObject *receiver,
                                PyObject *callback, bool encodeName)
{
    QByteArray functionName;
    qsizetype numArgs = -1;

    const auto slotArgs = extractFunctionArgumentsFromSlot(callback);
    qsizetype useSelf = slotArgs.isMethod ? 1 : 0;

    if (slotArgs.function != nullptr) {
        numArgs = argCount(slotArgs).max;
#ifdef PYPY_VERSION
    } else if (Py_TYPE(callback) == PepBuiltinMethod_TypePtr) {
        // PYSIDE-535: PyPy has a special builtin method that acts almost like PyCFunction.
        Shiboken::AutoDecRef temp(PyObject_GetAttr(callback, Shiboken::PyMagicName::name()));
        functionName = Shiboken::String::toCString(temp);
        useSelf = true;

        if (receiver) {
            // Search for signature on metaobject
            const QMetaObject *mo = receiver->metaObject();
            QByteArray prefix(functionName);
            prefix += '(';
            for (int i = 0; i < mo->methodCount(); i++) {
                QMetaMethod me = mo->method(i);
                if ((std::strncmp(me.methodSignature(), prefix, prefix.size()) == 0) &&
                    QMetaObject::checkConnectArgs(signal, me.methodSignature())) {
                    numArgs = me.parameterTypes().size() + useSelf;
                    break;
                }
           }
        }
#endif
    } else if (PyCFunction_Check(callback)) {
        functionName = PepCFunction_GET_NAMESTR(callback);
        useSelf = PyCFunction_GetSelf(callback) != nullptr ? 1 : 0;
        const int flags = PyCFunction_GetFlags(callback);

        if (receiver) {
            // Search for signature on metaobject
            const QMetaObject *mo = receiver->metaObject();
            for (int i = 0, count = mo->methodCount(); i < count; ++i) {
                QMetaMethod me = mo->method(i);
                if (functionName == me.name() &&
                    QMetaObject::checkConnectArgs(signal, me)) {
                    numArgs = me.parameterTypes().size() + useSelf;
                    break;
                }
           }
        }

        if (numArgs == -1) {
            if (flags & METH_VARARGS)
                numArgs = -1;
            else if (flags & METH_NOARGS)
                numArgs = 0;
        }
    } else if (PyCallable_Check(callback)) {
        functionName = "__callback" + QByteArray::number(quintptr(callback));
    }

    if (functionName.isEmpty() && slotArgs.functionName != nullptr)
        functionName = Shiboken::String::toCString(slotArgs.functionName);
    Q_ASSERT(!functionName.isEmpty());

    if (functionName.startsWith('<') && functionName.endsWith('>')) { // fix "<lambda>"
        functionName[0] = '_';
        functionName[functionName.size() - 1] = '_';
    }
    QByteArray signature = encodeName ? codeCallbackName(callback, functionName) : functionName;

    signature.append(u'(');

    int slotArgumentCount = signal.parameterCount();
    if (numArgs != -1 && slotArgumentCount > (numArgs - useSelf))
        slotArgumentCount = numArgs - useSelf;

    for (int i = 0; i < slotArgumentCount; ++i) {
        if (i > 0)
            signature.append(',');
        signature.append(signal.parameterTypeName(i));
    }
    signature.append(')');

    return signature;
}

bool isQtSignal(const char *signature)
{
    return (signature && signature[0] == QT_SIGNAL_SENTINEL);
}

bool checkQtSignal(const char *signature)
{
    if (!isQtSignal(signature)) {
        PyErr_SetString(PyExc_TypeError, "Use the function PySide6.QtCore.SIGNAL on signals");
        return false;
    }
    return true;
}

QByteArray codeCallbackName(PyObject *callback, const QByteArray &funcName)
{
    if (PyMethod_Check(callback)) {
        PyObject *self = PyMethod_GET_SELF(callback);
        PyObject *func = PyMethod_GET_FUNCTION(callback);
        return funcName + QByteArray::number(quint64(self), 16) + QByteArray::number(quint64(func), 16);
    }
    // PYSIDE-1523: Handle the compiled case.
    if (Shiboken::isCompiledMethod(callback)) {
        // Not retaining references inline with what PyMethod_GET_(SELF|FUNC) does.
        Shiboken::AutoDecRef self(PyObject_GetAttr(callback, PySide::PySideName::im_self()));
        Shiboken::AutoDecRef func(PyObject_GetAttr(callback, PySide::PySideName::im_func()));
        return funcName + QByteArray::number(quint64(self), 16) + QByteArray::number(quint64(func), 16);
    }
    return funcName + QByteArray::number(quint64(callback), 16);
}

QByteArray voidType()
{
    return QByteArrayLiteral("void");
}

} //namespace PySide::Signal
