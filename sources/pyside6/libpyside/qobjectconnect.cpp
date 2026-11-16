// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "qobjectconnect.h"
#include "dynamicslot_p.h"
#include "pysideqobject.h"
#include "pysideqslotobject_p.h"
#include "pysidesignal.h"
#include "pysideutils.h"
#include "signalmanager.h"
#include "signalmanager_p.h"

#include <sbkstring.h>
#include <sbkpep.h>
#include <sbkstaticstrings.h>
#include "basewrapper.h"
#include "autodecref.h"
#include <helper.h>

#include <QtCore/qdebug.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qobject.h>

#include <QtCore/private/qobject_p.h>

#include <string_view>

static bool isMethodDecorator(PyObject *method, bool is_pymethod, PyObject *self)
{
    Shiboken::AutoDecRef methodName(PyObject_GetAttr(method, Shiboken::PyMagicName::name()));

    Shiboken::AutoDecRef otherMethod(PySide::SignalManager::methodGetAttr(self, methodName));
    if (otherMethod.isNull()) {
        PyErr_Clear();
        return true;
    }

    // PYSIDE-1523: Each could be a compiled method or a normal method here, for the
    // compiled ones we can use the attributes.
    PyObject *function1{};
    if (PyMethod_Check(otherMethod.object())) {
        function1 = PyMethod_GET_FUNCTION(otherMethod.object());
    } else {
        function1 = PyObject_GetAttr(otherMethod.object(), Shiboken::PyName::im_func());
        if (function1 == nullptr)
            return false;
        Py_DECREF(function1);
        // Not retaining a reference in line with what PyMethod_GET_FUNCTION does.
    }

    PyObject *function2{};
    if (is_pymethod) {
        function2 = PyMethod_GET_FUNCTION(method);
    } else {
        function2 = PyObject_GetAttr(method, Shiboken::PyName::im_func());
        Py_DECREF(function2);
        // Not retaining a reference in line with what PyMethod_GET_FUNCTION does.
    }

    return function1 != function2;
}

struct GetReceiverResult
{
    QObject *receiver = nullptr;
    PyObject *self = nullptr;
    QByteArray callbackSig;
    bool forceDynamicSlot = false;
    int slotIndex = -1;
};

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const GetReceiverResult &r)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "GetReceiverResult(receiver=" << r.receiver << ", self=" << r.self
      << ", forceDynamicSlot=" << r.forceDynamicSlot
      << ", sig=\"" << r.callbackSig << "\", slotIndex=" << r.slotIndex << ')';
    return d;
}
#endif // QT_NO_DEBUG_STREAM

static const char *getQualifiedName(PyObject *ob)
{
    Shiboken::AutoDecRef qualNameP(PyObject_GetAttr(ob, Shiboken::PyMagicName::qualname()));
    return qualNameP.isNull()
                ? nullptr : Shiboken::String::toCString(qualNameP.object());
}

// Determine whether a method is declared in a class using qualified name lookup.
static bool isDeclaredIn(PyObject *method, const char *className)
{
    bool result = false;
    if (const auto *qualifiedNameC = getQualifiedName(PyMethod_Function(method))) {
        std::string_view qualifiedName(qualifiedNameC);
        if (const auto dot = qualifiedName.rfind('.'); dot != std::string::npos)
            result = qualifiedName.substr(0, dot) == className;
    }
    return result;
}

static GetReceiverResult getReceiver(QMetaMethod signal, PyObject *callback)
{
    GetReceiverResult result;

    if (PyMethod_Check(callback)) {
        result.self = PyMethod_GET_SELF(callback);
        result.receiver = PySide::convertToQObject(result.self, false);
        // Prevent dynamic slot creation for decorators
        result.forceDynamicSlot = isMethodDecorator(callback, true, result.self);
#ifdef PYPY_VERSION
    } else if (Py_TYPE(callback) == PepBuiltinMethod_TypePtr) {
        result.self = PyObject_GetAttrString(callback, "__self__");
        Py_DECREF(result.self);
        result.receiver = PySide::convertToQObject(result.self, false);
#endif
    } else if (PyCFunction_Check(callback)) {
        result.self = PyCFunction_GetSelf(callback);
        result.receiver = PySide::convertToQObject(result.self, false);
    } else if (Shiboken::isCompiledMethod(callback)) {
        result.self = PyObject_GetAttr(callback, Shiboken::PyName::im_self());
        Py_DECREF(result.self);
        result.receiver = PySide::convertToQObject(result.self, false);
        // Prevent dynamic slot creation for decorators
        result.forceDynamicSlot = isMethodDecorator(callback, false, result.self);
    } else if (PyCallable_Check(callback)) {
        // Ok, just a callable object
        result.receiver = nullptr;
        result.self = nullptr;
    }

    result.callbackSig =
        PySide::Signal::getCallbackSignature(signal, result.receiver, callback,
                                             false);

    // Check if this callback is a overwrite of a non-virtual Qt slot (pre-Jira bug 1019).
    // Make it possible to connect to a MyWidget.show() although QWidget.show()
    // is a non-virtual slot which would be found by QMetaObject search.
    // FIXME PYSIDE7: This is arguably a bit of a misguided "feature", remove?
    if (result.receiver && result.self) {
        const QMetaObject *metaObject = result.receiver->metaObject();
        result.slotIndex = metaObject->indexOfSlot(result.callbackSig.constData());
        if (PyMethod_Check(callback) != 0 && result.slotIndex != -1
            && result.slotIndex < metaObject->methodOffset()) {
             // Find the class in which the slot is declared.
             while (result.slotIndex < metaObject->methodOffset())
                 metaObject = metaObject->superClass();
             // If the Python callback is not declared in the same class, assume it is
             // a Python override. Resort to global receiver (PYSIDE-2418).
             if (!isDeclaredIn(callback, metaObject->className())) {
                 result.receiver = nullptr;
                 result.slotIndex = -1;
             }
         }
    }

    return result;
}

namespace PySide
{
class FriendlyQObject : public QObject // Make protected connectNotify() accessible.
{
public:
    using QObject::connectNotify;
    using QObject::disconnectNotify;
};

QMetaObject::Connection qobjectConnect(QObject *source, const char *signal,
                                       QObject *receiver, const char *slot,
                                       Qt::ConnectionType type)
{
    if (!signal || !slot || !PySide::Signal::checkQtSignal(signal))
        return {};

    if (!PySide::SignalManager::registerMetaMethod(source, signal + 1, QMetaMethod::Signal))
        return {};

    const auto methodType = PySide::Signal::isQtSignal(slot)
                            ? QMetaMethod::Signal : QMetaMethod::Slot;
    PySide::SignalManager::registerMetaMethod(receiver, slot + 1, methodType);
    return QObject::connect(source, signal, receiver, slot, type);
}

QMetaObject::Connection qobjectConnect(QObject *source, QMetaMethod signal,
                                       QObject *receiver, QMetaMethod slot,
                                       Qt::ConnectionType type)
{
    PySide::SignalManager::registerMetaMethod(receiver,
                                              slot.methodSignature().constData(),
                                              slot.methodType());
    return QObject::connect(source, signal, receiver, slot, type);
}

QMetaObject::Connection qobjectConnectCallback(QObject *source, const char *signal,
                                               PyObject *callback, Qt::ConnectionType type)
{
    if (!signal || !PySide::Signal::checkQtSignal(signal))
        return {};

    const int signalIndex =
        PySide::SignalManager::registerMetaMethodGetIndex(source, signal + 1,
                                                          QMetaMethod::Signal);
    if (signalIndex == -1)
        return {};

    // Extract receiver from callback
    const QMetaMethod signalMethod = source->metaObject()->method(signalIndex);
    GetReceiverResult receiver = getReceiver(signalMethod, callback);
    if (!receiver.forceDynamicSlot && receiver.receiver != nullptr && receiver.slotIndex == -1) {
        receiver.slotIndex = PySide::SignalManager::registerMetaMethodGetIndexBA(receiver.receiver,
                                                                                 receiver.callbackSig,
                                                                                 QMetaMethod::Slot);
    }

    QMetaObject::Connection connection{};
    const bool connectByIndex = !receiver.forceDynamicSlot
                                && receiver.receiver != nullptr && receiver.slotIndex != -1;
    Py_BEGIN_ALLOW_THREADS // PYSIDE-2367, prevent threading deadlocks with connectNotify()
    if (connectByIndex) {
        connection = QMetaObject::connect(source, signalIndex,
                                          receiver.receiver, receiver.slotIndex, type);
    } else {
        auto parameterTypes = signalMethod.parameterTypes();
        // Slots might have fewer arguments.
        if (!receiver.callbackSig.isEmpty()) {
            const auto paramCount = receiver.callbackSig.endsWith("()")
                                    ? qsizetype(0) : receiver.callbackSig.count(',') + 1;
            if (parameterTypes.size() > paramCount)
                parameterTypes.resize(paramCount);
        }
        auto *slotObject = new PySideQSlotObject(callback,
                                                 parameterTypes,
                                                 signalMethod.typeName());
        connection = QObjectPrivate::connect(source, signalIndex, slotObject, type);
    }
    Py_END_ALLOW_THREADS
    if (!connection)
        return {};

    if (!connectByIndex)
        registerSlotConnection(source, signalIndex, callback, connection);

    static_cast<FriendlyQObject *>(source)->connectNotify(signalMethod);
    return connection;
}

QMetaObject::Connection qobjectConnectCallback(QObject *source, const char *signal, QObject *context,
                                               PyObject *callback, Qt::ConnectionType type)
{
    if (!signal || !PySide::Signal::checkQtSignal(signal))
        return {};

    const int signalIndex =
        PySide::SignalManager::registerMetaMethodGetIndex(source, signal + 1,
                                                          QMetaMethod::Signal);
    if (signalIndex == -1)
        return {};

    const QMetaMethod signalMethod = source->metaObject()->method(signalIndex);
    auto *slotObject = new PySideQSlotObject(callback,
                                             signalMethod.parameterTypes(),
                                             signalMethod.typeName());

    QMetaObject::Connection connection{};
    Py_BEGIN_ALLOW_THREADS // PYSIDE-2367, prevent threading deadlocks with connectNotify()
    connection = QObjectPrivate::connect(source, signalIndex, context, slotObject, type);
    Py_END_ALLOW_THREADS
    if (!connection)
        return {};

    registerSlotConnection(source, signalIndex, callback, connection);

    static_cast<FriendlyQObject *>(source)->connectNotify(signalMethod);
    return connection;
}

bool qobjectDisconnectCallback(QObject *source, const char *signal, PyObject *callback)
{
    if (!PySide::Signal::checkQtSignal(signal))
        return false;

    const auto *metaObject = source->metaObject();
    const int signalIndex = metaObject->indexOfSignal(signal + 1);
    if (signalIndex == -1)
        return false;

    if (!disconnectSlot(source, signalIndex, callback)) {
        // PYSIDE-3020: Check for disconnecting a string-based connection by passing a callable?
        auto receiver = getReceiver(metaObject->method(signalIndex), callback);
        if (receiver.receiver == nullptr || receiver.slotIndex == -1
            || !QMetaObject::disconnect(source, signalIndex,
                                        receiver.receiver, receiver.slotIndex)) {
            return false;
        }
    }

    const QMetaMethod signalMethod = metaObject->method(signalIndex);
    static_cast<FriendlyQObject *>(source)->disconnectNotify(signalMethod);
    return true;
}

bool callConnect(PyObject *self, const char *signal, PyObject *argument)
{
    using Shiboken::AutoDecRef;

    if (PyObject_TypeCheck(argument, PySideSignalInstance_TypeF()) == 0) {
        AutoDecRef result(PyObject_CallMethod(self, "connect", "OsO", self, signal, argument));
        return !result.isNull();
    }

    // Connecting signal to signal
    auto *signalInstance = reinterpret_cast<PySideSignalInstance *>(argument);
    AutoDecRef signalSignature(Shiboken::String::fromFormat("2%s", PySide::Signal::getSignature(signalInstance)));
    AutoDecRef result(PyObject_CallMethod(self, "connect", "OsOO", self, signal,
                                          PySide::Signal::getObject(signalInstance),
                                          signalSignature.object()));
    return !result.isNull();
}

} // namespace PySide
