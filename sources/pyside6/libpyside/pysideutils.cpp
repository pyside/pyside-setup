// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideutils.h"

#include <autodecref.h>
#include <helper.h>
#include <pep384ext.h>
#include <sbkpep.h>
#include <sbkpydictrange.h>
#include <sbkpysequencerange.h>
#include <sbkstaticstrings.h>

#include <QtCore/qdir.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringview.h>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace PySide
{

const char *const execDeprecatedMsg =
    "'exec_' will be removed in the future. Use 'exec' instead.";

bool inherits(PyTypeObject *objType, const char *class_name)
{
    if (std::strcmp(PepType_GetFullyQualifiedNameStr(objType), class_name) == 0)
        return true;

    PyTypeObject *base = objType->tp_base;
    if (base == nullptr)
        return false;

    return inherits(base, class_name);
}

QString pyUnicodeToQString(PyObject *str)
{
    Q_ASSERT(PyUnicode_Check(str) != 0);

    const void *data = _PepUnicode_DATA(str);
    const Py_ssize_t len = PyUnicode_GetLength(str);
    switch (_PepUnicode_KIND(str)) {
    case PepUnicode_1BYTE_KIND:
        return QString::fromLatin1(reinterpret_cast<const char *>(data), len);
    case PepUnicode_2BYTE_KIND:
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data), len);
    case PepUnicode_4BYTE_KIND:
        break;
    }
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(data), len);
}

PyObject *qStringToPyUnicode(QStringView s)
{
    const QByteArray ba = s.toUtf8();
    return PyUnicode_FromStringAndSize(ba.constData(), ba.size());
}

// Inspired by Shiboken::String::toCString;
QString pyStringToQString(PyObject *str)
{
    if (str == Py_None)
        return {};

    if (PyUnicode_Check(str) != 0)
        return pyUnicodeToQString(str);

    if (PyBytes_Check(str)) {
        const char *asciiBuffer = PyBytes_AsString(str);
        if (asciiBuffer != nullptr)
            return QString::fromLatin1(asciiBuffer);
    }
    return {};
}

// PySide-1499: Provide an efficient, correct PathLike interface
QString pyPathToQString(PyObject *path)
{
    // For empty constructors path can be nullptr
    // fallback to an empty QString in that case.
    if (path == nullptr)
        return {};

    // str or bytes pass through
    if (PyUnicode_Check(path) || PyBytes_Check(path))
        return pyStringToQString(path);

    // Let PyOS_FSPath do its work and then fix the result for Windows.
    Shiboken::AutoDecRef strPath(PyOS_FSPath(path));
    if (strPath.isNull())
        return {};
    return QDir::fromNativeSeparators(pyStringToQString(strPath));
}

bool isCompiledMethod(PyObject *callback)
{
    return Shiboken::isCompiledMethod(callback);
}

QString sysExecutable()
{
#ifdef Py_LIMITED_API
    Shiboken::AutoDecRef result(PepExt_EvalString("sys", "executable"));
    if (result.isNull()) {
        qWarning("libpyside: Unable to determine sys.executable.");
        PyErr_Print();
        return {};
    }
    return QDir::cleanPath(pyUnicodeToQString(result.object()));
#else
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyConfig_Read(&config);
    return QDir::cleanPath(QString::fromWCharArray(config.executable));
#endif
}

// An approximation of "Unicode Standard Annex #31" for language identifiers to prevent code
// injection attacks (cf uic). FIXME Simplify according to QTBUG-126860?
static bool isIdStart(QChar c)
{
    bool result = false;
    switch (c.category()) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_Letter:
        result = true;
        break;
    default:
        result = c == u'_';
        break;
    }
    return result;
}

static bool isIdContinuation(QChar c)
{
    bool result = false;
    switch (c.category()) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_Letter:
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Number_DecimalDigit:
    case QChar::Punctuation_Connector: // '_'
        result = true;
        break;
    default:
        break;
    }
    return result;
}

static bool isUiClassNameContinuation(QChar c)
{
    return c == u':' || c == u'.' || isIdContinuation(c);
}

bool isUiClassName(QStringView name)
{
    return !name.isEmpty() && isIdStart(name.at(0))
    && std::all_of(name.cbegin() + 1, name.cend(), isUiClassNameContinuation);
}

debugPyTypeObject::debugPyTypeObject(PyTypeObject *o) noexcept
    : m_object(o)
{
}

QDebug operator<<(QDebug debug, const debugPyTypeObject &o)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "PyTypeObject(";
    if (o.m_object != nullptr)
        debug << '"' << PepType_GetFullyQualifiedNameStr(o.m_object) << '"';
    else
        debug << '0';
    debug << ')';
    return debug;
}

static void formatPyObject(PyObject *obj, QDebug &debug);

static void formatPySequence(PyObject *obj, QDebug &debug)
{
    Shiboken::PySequenceRange range(obj);
    debug << range.size() << " [";
    bool first = true;
    for (const auto &item : range) {
        if (item.isNull()) { // Debug output must not leave an error set
            PyErr_Clear();
            break;
        }
        if (first)
            first = false;
        else
            debug << ", ";
        formatPyObject(item.object(), debug);
    }
    debug << ']';
}

static void formatPyDict(PyObject *obj, QDebug &debug)
{
    bool first = true;
    debug << '{';
    for (auto [key, value] : Shiboken::PyDictRange(obj)) {
        if (first)
            first = false;
        else
            debug << ", ";
        formatPyObject(key, debug);
        debug << '=';
        formatPyObject(value, debug);
    }
    debug << '}';
}

static inline const char *pyTypeName(PyObject *obj)
{
    return Py_TYPE(obj)->tp_name;
}

static QString getQualName(PyObject *obj)
{
    Shiboken::AutoDecRef result(PyObject_GetAttr(obj, Shiboken::PyMagicName::qualname()));
    return result.object() != nullptr
        ? pyStringToQString(result.object()) : QString{};
}

static void formatPyFunction(PyObject *obj, QDebug &debug)
{
    debug << '"' << getQualName(obj) << "()\"";
}

static void formatPyMethod(PyObject *obj, QDebug &debug)
{
    if (auto *func = PyMethod_Function(obj))
        formatPyFunction(func, debug);
    debug << ", instance=" << PyMethod_Self(obj);
}

static void formatPyObjectValue(PyObject *obj, QDebug &debug)
{
    if (PyType_Check(obj) != 0)
        debug << "type: \"" << pyTypeName(obj) << '"';
    else if (PyLong_Check(obj) != 0) {
        const auto llv = PyLong_AsLongLong(obj);
        if (PyErr_Occurred() != PyExc_OverflowError) {
            debug << llv;
        } else {
            PyErr_Clear();
            debug << "0x" << Qt::hex << PyLong_AsUnsignedLongLong(obj) << Qt::dec;
        }
    } else if (PyFloat_Check(obj) != 0)
        debug << PyFloat_AsDouble(obj);
    else if (PyUnicode_Check(obj) != 0)
        debug << '"' << pyStringToQString(obj) << '"';
    else if (PyFunction_Check(obj) != 0)
        formatPyFunction(obj, debug);
    else if (PyMethod_Check(obj) != 0)
        formatPyMethod(obj, debug);
    else if (PySequence_Check(obj) != 0)
        formatPySequence(obj, debug);
    else if (PyDict_Check(obj) != 0)
        formatPyDict(obj, debug);
    else
        debug << obj;
}

static void formatPyObject(PyObject *obj, QDebug &debug)
{
    if (obj == nullptr) {
        debug << '0';
        return;
    }
    if (obj == Py_None) {
        debug << "None";
        return;
    }
    if (obj == Py_True) {
        debug << "True";
        return;
    }
    if (obj == Py_False) {
        debug << "False";
        return;
    }
    if (PyType_Check(obj) == 0)
        debug << pyTypeName(obj);
    const auto refs = Py_REFCNT(obj);
    if (refs == UINT_MAX) // _Py_IMMORTAL_REFCNT
        debug << ", immortal";
    else
        debug << ", refs=" << refs;
    debug << ": ";
    formatPyObjectValue(obj, debug);
}

debugPyObject::debugPyObject(PyObject *o) noexcept : m_object(o)
{
}

QDebug operator<<(QDebug debug, const debugPyObject &o)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "PyObject(";
    formatPyObject(o.m_object, debug);
    debug << ')';
    return debug;
}

#if !defined(Py_LIMITED_API) || Py_LIMITED_API >= 0x030B0000
static void formatPy_ssizeArray(QDebug &debug, const char *name, const Py_ssize_t *array, int len)
{
    debug << ", " << name << '=';
    if (array != nullptr) {
        debug << '[';
        for (int i = 0; i < len; ++i)
            debug << array[i] << ' ';
        debug << ']';
    } else {
        debug << '0';
    }
}

debugPyBuffer::debugPyBuffer(Py_buffer *b) noexcept : m_buffer(b)
{
}

PYSIDE_API QDebug operator<<(QDebug debug, const debugPyBuffer &b)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "Py_buffer(";
    if (b.m_buffer != nullptr) {
        debug << "obj=" << b.m_buffer->obj
              << ", buf=" << b.m_buffer->buf << ", len=" << b.m_buffer->len
              << ", readonly=" <<  b.m_buffer->readonly
              << ", itemsize=" <<  b.m_buffer->itemsize << ", format=";
        if (b.m_buffer->format != nullptr)
            debug << '"' << b.m_buffer->format << '"';
        else
            debug << '0';
        debug << ", ndim=" << b.m_buffer->ndim;
        formatPy_ssizeArray(debug, "shape", b.m_buffer->shape, b.m_buffer->ndim);
        formatPy_ssizeArray(debug, "strides", b.m_buffer->strides, b.m_buffer->ndim);
        formatPy_ssizeArray(debug, "suboffsets", b.m_buffer->suboffsets, b.m_buffer->ndim);
    } else {
        debug << '0';
    }
    debug << ')';
    return debug;
}
#endif // !Py_LIMITED_API || >= 3.11

} // namespace PySide
