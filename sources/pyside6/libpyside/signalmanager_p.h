// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SIGNALMANAGER_P_H
#define SIGNALMANAGER_P_H

#include <sbkpython.h>

#include <QtCore/qmetaobject.h>

namespace PySide::SignalManager
{
    void init();

    int registerMetaMethodGetIndex(QObject *source, const char *signature,
                                   QMetaMethod::MethodType type);
    int registerMetaMethodGetIndexBA(QObject *source, const QByteArray &signature,
                                     QMetaMethod::MethodType type);

    // Utility function to call a python method using args received in qt_metacall
    int callPythonMetaMethod(QMetaMethod method, void **args, PyObject *callable);
    int callPythonMetaMethod(const QByteArrayList &parameterTypes,
                             const char *returnType /* = nullptr */,
                             void **args, PyObject *callable);

    PyObject *methodGetAttr(PyObject *self, PyObject *name);

    void handleMetaCallError();
} // namespace PySide::SignalManager

#ifdef Py_GIL_DISABLED
namespace PySide
{
    class MetaObjectBuilder;
}

namespace PySide::SignalManager
{
    const QMetaObject *updateTypeMetaObject(MetaObjectBuilder &builder);
} // namespace PySide::SignalManager
#endif

#endif // SIGNALMANAGER_P_H
