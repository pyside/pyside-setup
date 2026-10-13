// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDEQOBJECT_H
#define PYSIDEQOBJECT_H

#include <sbkpython.h>

#include <pysidemacros.h>

#include <QtCore/qtclasshelpermacros.h>

#include <cstddef>

QT_FORWARD_DECLARE_CLASS(QObject)
QT_FORWARD_DECLARE_STRUCT(QMetaObject)
QT_FORWARD_DECLARE_CLASS(QMutex)

namespace PySide
{

/*!
    Returns the PyTypeObject for QObject.
*/
PYSIDE_API PyTypeObject *qObjectType();

/*!
    Sets the PyTypeObject for QObject to \a t (called from QtCore module).
*/
PYSIDE_API void setQObjectType(PyTypeObject *t);

PYSIDE_API bool fillQtProperties(PyObject *qObj, const QMetaObject *metaObj,
                                 PyObject *kwds, bool allowErrors);

/*!
    Initializes the DynamicMetaObject helper for a wrapped Qt \a type
    (generated code) with the meta object \a base of the C++ class
    of size \a cppObjSize.
*/
PYSIDE_API void initDynamicMetaObject(PyTypeObject *type, const QMetaObject *base,
                                      std::size_t cppObjSize);
/*!
    Initializes a Python-derived \a type created with the
    arguments \a args and \a kwds. On free-threaded builds, it may wait
    for the lock of the meta-object builders, attached.
*/
PYSIDE_API void initQObjectSubType(PyTypeObject *type, PyObject *args, PyObject *kwds);

/*!
    Returns the size in bytes of a \a type that inherits QObject.
*/
PYSIDE_API std::size_t getSizeOfQObject(PyTypeObject *type);

/*!
    Returns whether the PyTypeObject \a pyType or its bases contain
    a QObject. \a raiseError controls whether a TypeError is raised
    when the type does not inherit QObject.
*/
PYSIDE_API bool isQObjectDerived(PyTypeObject *pyType, bool raiseError);

/*!
    Convenience to convert a PyObject \a object to QObject.
    \a raiseError controls whether a TypeError is raised
    when the conversion fails.
*/
PYSIDE_API QObject *convertToQObject(PyObject *object, bool raiseError);

PYSIDE_API PyObject *getHiddenDataFromQObject(QObject *cppSelf, PyObject *self, PyObject *name);

/*!
    Returns the mutex for accessing QObject memory helpers from
    multiple threads.
*/
PYSIDE_API QMutex &nextQObjectMemoryAddrMutex();
PYSIDE_API void *nextQObjectMemoryAddr();
/*!
    Sets the address \a addr where to allocate the next QObject (for QML).
*/
PYSIDE_API void setNextQObjectMemoryAddr(void *addr);

PYSIDE_API PyObject *getWrapperForQObject(QObject *cppSelf, PyTypeObject *sbk_type);

/*!
    Returns the best-matching type for the QObject \a cppSelf
    (Helper for \c{QObject.findType()}).
*/
PYSIDE_API PyTypeObject *getTypeForQObject(const QObject *cppSelf);

} //namespace PySide

// Deletion handler for "delete-in-owner-thread"
extern "C" PYSIDE_API void deferredDeleteQObject(void *cppSelf);

#endif // PYSIDEQOBJECT_H
