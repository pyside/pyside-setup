// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkstring.h"
#include "sbkpep.h"
#include "sbkenum.h"
#include "sbkstaticstrings_p.h"
#include "autodecref.h"

#include <cstring>
#include <cctype>

#ifdef Py_GIL_DISABLED
#  include "sbkheldlocks.h"
#endif

/*!
    \namespace Shiboken::String

    \brief Namespace for string-related functionality.

    \inmodule libshiboken
*/
namespace Shiboken::String
{

// PYSIDE-795: Redirecting PySequence to Iterable
bool checkIterable(PyObject *obj)
{
    return PyObject_HasAttr(obj, Shiboken::PyMagicName::iter());
}

bool checkIterableArgument(PyObject *obj)
{
    return checkIterable(obj) && !Shiboken::Enum::check(obj);
}

static PyObject *initPathLike()
{
    if (auto *osmodule = PyImport_ImportModule("os")) {
        if (PyObject *PathLike = PyObject_GetAttrString(osmodule, "PathLike"))
            return PathLike;
    }
    PyErr_Print();
    Py_FatalError("libshiboken: cannot import os.PathLike");
    return nullptr;
}

// PYSIDE-1499: Migrate to pathlib.Path and support __fspath__ in PySide
bool checkPath(PyObject *path)
{
    // Let normal strings through, unchanged.
    if (PyUnicode_Check(path) || PyBytes_Check(path))
        return true;
    // Without the Limited API, we could look up an `__fspath__` class attribute.
    // But we use `isinstance(os.PathLike)`, instead.
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyObject *> PathLikeOnce;
    PyObject *PathLike = PathLikeOnce.get(initPathLike);
#else
    static PyObject *PathLike = initPathLike();
#endif
    return PyObject_IsInstance(path, PathLike);
}

bool checkType(PyTypeObject *type)
{
    return type == &PyUnicode_Type;
}

bool check(PyObject *obj)
{
    return obj == Py_None || PyUnicode_Check(obj);
}

bool checkChar(PyObject *pyobj)
{
    return check(pyobj) && (len(pyobj) == 1);
}

bool isConvertible(PyObject *obj)
{
    return check(obj);
}

PyObject *fromCString(const char *value)
{
    return PyUnicode_FromString(value);
}

PyObject *fromCString(const char *value, int len)
{
    return PyUnicode_FromStringAndSize(value, len);
}

const char *toCString(PyObject *str)
{
    if (str == Py_None)
        return nullptr;
    if (PyUnicode_Check(str))
        return PyUnicode_AsUTF8AndSize(str, nullptr);
    if (PyBytes_Check(str))
        return PyBytes_AsString(str);
    return nullptr;
}

const char *toCString(PyObject *str, Py_ssize_t *len)
{
    if (str == Py_None) {
        *len = 0;
        return nullptr;
    }
    if (PyUnicode_Check(str)) {
       // We need to encode the unicode string into utf8 to know the size of returned char *.
       Shiboken::AutoDecRef uniStr(PyUnicode_AsUTF8String(str));
       *len = PyBytes_Size(uniStr.object());
        // Return unicode from str instead of uniStr, because the lifetime of the returned pointer
        // depends on the lifetime of str.
        return PyUnicode_AsUTF8AndSize(str, nullptr);
    }
    if (PyBytes_Check(str)) {
        *len = PyBytes_Size(str);
        return PyBytes_AsString(str);
    }
    return nullptr;
}

bool concat(PyObject **val1, PyObject *val2)
{
    if (PyUnicode_Check(*val1) && PyUnicode_Check(val2)) {
        PyObject *result = PyUnicode_Concat(*val1, val2);
        Py_DECREF(*val1);
        *val1 = result;
        return true;
    }

    if (PyBytes_Check(*val1) && PyBytes_Check(val2)) {
        PyBytes_Concat(val1, val2);
        return true;
    }

    return false;
}

PyObject *fromFormat(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    PyObject *result = nullptr;
    result = PyUnicode_FromFormatV(format, argp);
    va_end(argp);
    return result;
}

PyObject *fromStringAndSize(const char *str, Py_ssize_t size)
{
    return PyUnicode_FromStringAndSize(str, size);
}

int compare(PyObject *val1, const char *val2)
{
    if (PyUnicode_Check(val1))
       return PyUnicode_CompareWithASCIIString(val1, val2);
    return 0;

}

Py_ssize_t len(PyObject *str)
{
    if (str == Py_None)
        return 0;

    if (PyUnicode_Check(str))
        return PyUnicode_GetLength(str);

    if (PyBytes_Check(str))
        return PyBytes_Size(str);
    return 0;
}

///////////////////////////////////////////////////////////////////////
//
// Implementation of efficient Python strings
// ------------------------------------------
//
// Instead of repetitively executing
//
//     PyObject *attr = PyObject_GetAttrString(obj, "__name__");
//
// a helper of the form
//
// PyObject *name()
// {
//    static PyObject *const s = Shiboken::String::createStaticString("__name__");
//    return result;
// }
//
// can now be implemented, which registers the string into a static set avoiding
// repetitive string creation. The resulting code looks like:
//
//     PyObject *attr = PyObject_GetAttr(obj, name());
//

PyObject *createStaticString(const char *str)
{
     return PyUnicode_InternFromString(str);
}

///////////////////////////////////////////////////////////////////////
//
// PYSIDE-1019: Helper function for snake_case vs. camelCase names
// ---------------------------------------------------------------
//
// When renaming dict entries, `BindingManager::getOverride` must
// use adapted names.
//
// This might become more complex when we need to register
// exceptions from this rule.
//

PyObject *getSnakeCaseName(const char *name, bool lower)
{
    /*
     * Convert `camelCase` to `snake_case`.
     * Gives up when there are two consecutive upper chars.
     *
     * Also functions beginning with `gl` followed by upper case stay
     * unchanged since that are the special OpenGL functions.
     */
    if (!lower
        || std::strlen(name) < 3
        || (name[0] == 'g' && name[1] == 'l' && std::isupper(name[2])))
        return createStaticString(name);

    char new_name[200 + 1] = {};
    const char *p = name;
    char *q = new_name;
    for (; *p && q - new_name < 200; ++p, ++q) {
        if (std::isupper(*p)) {
            if (p != name && std::isupper(*(p - 1)))
                return createStaticString(name);
            *q = '_';
            ++q;
            *q = std::tolower(*p);
        }
        else {
            *q = *p;
        }
    }
    return createStaticString(new_name);
}

PyObject *getSnakeCaseName(PyObject *name, bool lower)
{
    // This is all static strings, not refcounted.
    if (lower)
        return getSnakeCaseName(toCString(name), lower);
    return name;
}

// Return a generic representation of a PyObject as does PyObject_Repr().
// Note: PyObject_Repr() may not be called on self from __repr__() as this
// causes a recursion.
PyObject *repr(PyObject *o)
{
    if (o == nullptr)
        return PyUnicode_FromString("<NULL>");
    if (o == Py_None)
        return PyUnicode_FromString("None");
     return PyUnicode_FromFormat("<%s object at %p>", Py_TYPE(o)->tp_name, o);
}

} // namespace Shiboken::String
