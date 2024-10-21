// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef SIGNALMANAGER_H
#define SIGNALMANAGER_H

#include "pysidemacros.h"

#include <sbkpython.h>
#include <shibokenmacros.h>

#include <QtCore/QMetaMethod>

#include <optional>

QT_FORWARD_DECLARE_CLASS(QDataStream)

namespace PySide
{

/// Thin wrapper for PyObject which increases the reference count at the constructor but *NOT* at destructor.
class PYSIDE_API PyObjectWrapper
{
public:
    PyObjectWrapper(PyObjectWrapper&&) = delete;
    PyObjectWrapper& operator=(PyObjectWrapper &&) = delete;

    PyObjectWrapper();
    explicit PyObjectWrapper(PyObject* me);
    PyObjectWrapper(const PyObjectWrapper &other);
    PyObjectWrapper& operator=(const PyObjectWrapper &other);

    void reset(PyObject *o);

    ~PyObjectWrapper();
    operator PyObject*() const;

    // FIXME: To be removed in Qt7
    // This was done to make QAbstractItemModel::data() work without explicit conversion of
    // QVariant(PyObjectWrapper) to QVariant(int). This works because QAbstractItemModel::data()
    // inturn calls legacyEnumValueFromModelData(const QVariant &data). But this function will
    // be removed in Qt7.
    // The proper fix would be to associate PyObjectWrapper to the corresponding C++ Enum.
    int toInt() const;

private:
    PyObject* m_me;
};

PYSIDE_API QDataStream &operator<<(QDataStream& out, const PyObjectWrapper& myObj);
PYSIDE_API QDataStream &operator>>(QDataStream& in, PyObjectWrapper& myObj);

class PYSIDE_API SignalManager
{
public:
    Q_DISABLE_COPY_MOVE(SignalManager)

    using QmlMetaCallErrorHandler = std::optional<int>(*)(QObject *object);

    static void init();

    static void setQmlMetaCallErrorHandler(QmlMetaCallErrorHandler handler);

    static bool emitSignal(QObject* source, const char* signal, PyObject* args);
    static int qt_metacall(QObject* object, QMetaObject::Call call, int id, void** args);

    // Used to register a new signal/slot on QMetaobject of source.
    static bool registerMetaMethod(QObject* source, const char* signature,
                                   QMetaMethod::MethodType type);
    static int registerMetaMethodGetIndex(QObject* source, const char *signature,
                                          QMetaMethod::MethodType type);
    static int registerMetaMethodGetIndexBA(QObject* source, const QByteArray &signature,
                                            QMetaMethod::MethodType type);

    // used to discovery metaobject
    static const QMetaObject* retrieveMetaObject(PyObject* self);

    // Utility function to call a python method using args received in qt_metacall
    static int callPythonMetaMethod(QMetaMethod method, void **args, PyObject *callable);
    static int callPythonMetaMethod(const QByteArrayList &parameterTypes,
                                    const char *returnType /* = nullptr */,
                                    void **args, PyObject *callable);
    static void handleMetaCallError();
};

}

Q_DECLARE_METATYPE(PySide::PyObjectWrapper)

#endif
