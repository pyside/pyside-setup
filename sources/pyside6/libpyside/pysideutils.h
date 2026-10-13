// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDEUTILS_H
#define PYSIDEUTILS_H

#include <sbkpython.h>

#include <pysidemacros.h>

#include <QtCore/qtclasshelpermacros.h>

QT_FORWARD_DECLARE_CLASS(QDebug)
QT_FORWARD_DECLARE_CLASS(QString)
QT_FORWARD_DECLARE_CLASS(QStringView)

namespace PySide
{

/*!
    Returns whether the type \a self inherits from the class named
    \a class_name.
*/
PYSIDE_API bool inherits(PyTypeObject *self, const char *class_name);
PYSIDE_API bool wrapperInherits(const void *cppSelf, const char *class_name);

/*!
    Given a PyObject \a str representing Unicode data, returns an
    equivalent QString.
*/
PYSIDE_API QString pyUnicodeToQString(PyObject *str);

/*!
    Given a string \a s, returns the PyObject representing Unicode data.
*/
PYSIDE_API PyObject *qStringToPyUnicode(QStringView s);

/*!
    Given a PyObject \a str representing ASCII or Unicode data, returns
    an equivalent QString.
*/
PYSIDE_API QString pyStringToQString(PyObject *str);

/*!
    Provides an efficient, correct PathLike interface converting
    \a path to a QString.
*/
PYSIDE_API QString pyPathToQString(PyObject *path);

/*!
    Returns whether \a callback is a compiled method (Nuitka).
    See also \c{Shiboken::isCompiledMethod()}.
*/
PYSIDE_API bool isCompiledMethod(PyObject *callback);

/*!
    Returns the Python binary (value of \c sys.executable).
*/
PYSIDE_API QString sysExecutable();

/*!
    Returns whether \a name is a valid class name in a \c .ui file
    (C++ or Python).
*/
PYSIDE_API bool isUiClassName(QStringView name);

struct debugPyTypeObject
{
    PYSIDE_API explicit debugPyTypeObject(PyTypeObject *o) noexcept;

    PyTypeObject *m_object;
};

PYSIDE_API QDebug operator<<(QDebug debug, const debugPyTypeObject &o);

struct debugPyObject
{
    PYSIDE_API explicit debugPyObject(PyObject *o) noexcept;

    PyObject *m_object;
};

PYSIDE_API QDebug operator<<(QDebug debug, const debugPyObject &o);

#if !defined(Py_LIMITED_API) || Py_LIMITED_API >= 0x030B0000
struct debugPyBuffer
{
    PYSIDE_API explicit debugPyBuffer(Py_buffer *b) noexcept;

    Py_buffer *m_buffer;
};

PYSIDE_API QDebug operator<<(QDebug debug, const debugPyBuffer &b);
#endif // !Py_LIMITED_API || >= 3.11

// Shared deprecation message for exec_() → exec() across all Qt module bindings.
extern PYSIDE_API const char *const execDeprecatedMsg;

} //namespace PySide

#endif // PYSIDESTRING_H
