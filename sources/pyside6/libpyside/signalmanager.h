// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SIGNALMANAGER_H
#define SIGNALMANAGER_H

#include "pysidemacros.h"

#include <sbkpython.h>

#include <QtCore/qmetaobject.h>

#include <optional>

namespace PySide::SignalManager
{
    using QmlMetaCallErrorHandler = std::optional<int>(*)(QObject *object);

    PYSIDE_API void setQmlMetaCallErrorHandler(QmlMetaCallErrorHandler handler);

    PYSIDE_API bool emitSignal(QObject *source, const char* signal, PyObject *args);
    PYSIDE_API bool emitSignal(QObject *source, int signalIndex, PyObject *args);
    PYSIDE_API int qt_metacall(QObject *object, QMetaObject::Call call, int id, void **args);

    // Used to register a new signal/slot on QMetaobject of source.
    PYSIDE_API bool registerMetaMethod(QObject *source, const char *signature,
                                       QMetaMethod::MethodType type);

    // used to discover metaobject
    PYSIDE_API const QMetaObject *retrieveMetaObject(const QObject *cppSelf);

    // used to discover metaobject
    PYSIDE_API const QMetaObject *retrieveMetaObject(PyObject *self);


} // namespace PySide::SignalManager

#endif // SIGNALMANAGER_H
