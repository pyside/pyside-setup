// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDE_P_H
#define PYSIDE_P_H

#include <sbkpython.h>

#include <pysidemacros.h>

#include <dynamicqmetaobject.h>

#ifdef Py_GIL_DISABLED
#  include <atomic>
#endif

namespace PySide
{

#ifdef Py_GIL_DISABLED
struct SignalClassList;
#endif

// Struct associated with QObject's via Shiboken::Object::getTypeUserData()
struct TypeUserData
{
    explicit TypeUserData(PyTypeObject *type, const QMetaObject *metaobject, std::size_t size) :
        mo(type, metaobject), cppObjSize(size) {}
    // Plain wrapped Qt types
    explicit TypeUserData(const QMetaObject *metaobject, std::size_t size) :
        mo(metaobject), cppObjSize(size) {}

    MetaObjectBuilder mo;
    std::size_t cppObjSize;
#ifdef Py_GIL_DISABLED
    ~TypeUserData();

    // The newest list; see updateSourceObject()
    std::atomic<const SignalClassList *> signalList{nullptr};
#endif
};

PYSIDE_API TypeUserData *retrieveTypeUserData(PyTypeObject *pyTypeObj);
PYSIDE_API TypeUserData *retrieveTypeUserData(PyObject *pyObj);
// For QML
PYSIDE_API const QMetaObject *retrieveMetaObject(PyTypeObject *pyTypeObj);
PYSIDE_API const QMetaObject *retrieveMetaObject(PyObject *pyObj);
#ifdef Py_GIL_DISABLED
PYSIDE_API bool isDeletionPending(const QObject *qobject);
#endif

} //namespace PySide

#endif // PYSIDE_P_H
