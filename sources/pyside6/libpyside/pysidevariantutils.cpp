// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidevariantutils.h"
#include "pysideutils.h"

#include <QtCore/qvariantmap.h>

#include <autodecref.h>
#include <sbkconverter.h>
#include <sbkpep.h>
#include <sbkpydictrange.h>
#include <sbkpytuplerange.h>
#include <basewrapper.h>

using namespace Qt::StringLiterals;

static const char qVariantTypeName[] = "QVariant";

static void warnConverter(const char *name)
{
    qWarning("libpyside: Type converter for: %s not registered.", name);
}

// Helper converting each item of a non-empty list using the "QVariant" converter
static std::optional<QVariantList> pyListToVariantListHelper(PyObject *list, Py_ssize_t size)
{
    Q_ASSERT(size > 0);
    QVariantList result;
    result.reserve(size);
    Shiboken::Conversions::SpecificConverter converter(qVariantTypeName);
    if (!converter) {
        warnConverter(qVariantTypeName);
        return std::nullopt;
    }
    for (Py_ssize_t i = 0; i < size; ++i) {
        Shiboken::AutoDecRef pyItem(PySequence_GetItem(list, i));
        QVariant item;
        converter.toCpp(pyItem.object(), &item);
        result.append(item);
    }
    return result;
}

// Helper checking for a sequence of Unicode objects
static bool isStringList(PyObject *list)
{
    const Py_ssize_t size = PySequence_Size(list);
    if (size == 0)
        return false;
    for (Py_ssize_t i = 0; i < size; ++i) {
        Shiboken::AutoDecRef item(PySequence_GetItem(list, i));
        if (PyUnicode_Check(item) == 0)
            return false;
    }
    return true;
}

// Helper to convert to a QStringList
static std::optional<QStringList> listToStringList(PyObject *list)
{
    static const char listType[] = "QList<QString>";
    Shiboken::Conversions::SpecificConverter converter(listType);
    if (!converter) {
        warnConverter(listType);
        return std::nullopt;
    }
    QStringList result;
    converter.toCpp(list, &result);
    return result;
}

// Helper to convert a non-empty, homogenous list using the converter of the first item
static QVariant convertToValueList(PyObject *list)
{
    Q_ASSERT(PySequence_Size(list) >= 0);

    Shiboken::AutoDecRef element(PySequence_GetItem(list, 0));

    auto *type = reinterpret_cast<PyTypeObject *>(element.object());
    QMetaType metaType = PySide::Variant::resolveMetaType(type);
    if (!metaType.isValid())
        return {};

    const QByteArray listTypeName = QByteArrayLiteral("QList<") + metaType.name() + '>';
    metaType = QMetaType::fromName(listTypeName);
    if (!metaType.isValid())
        return {};

    Shiboken::Conversions::SpecificConverter converter(listTypeName);
    if (!converter) {
        warnConverter(listTypeName.constData());
        return {};
    }

    QVariant var(metaType);
    converter.toCpp(list, &var);
    return var;
}

/*!
    \namespace PySide::Variant
    \brief Namespace for QVariant-related code.
    \inmodule libpyside
*/
namespace PySide::Variant
{

QMetaType resolveMetaType(PyTypeObject *type)
{
    if (!PyObject_TypeCheck(reinterpret_cast<PyObject *>(type), SbkObjectType_TypeF()))
        return {};
    const char *typeName = Shiboken::ObjectType::getOriginalName(type);
    if (!typeName)
        return {};
    const bool valueType = '*' != typeName[qstrlen(typeName) - 1];
    // Do not convert user type of value
    if (valueType && Shiboken::ObjectType::isUserType(type))
        return {};
    QMetaType metaType = QMetaType::fromName(typeName);
    if (metaType.isValid())
        return metaType;
    // Do not resolve types to value type
    if (valueType)
        return {};
    // Find in base types. First check tp_bases, and only after check tp_base, because
    // tp_base does not always point to the first base class, but rather to the first
    // that has added any python fields or slots to its object layout.
    // See https://mail.python.org/pipermail/python-list/2009-January/520733.html
    if (Shiboken::PyTupleTypeRange basesRange(type->tp_bases); !basesRange.isEmpty()) {
        auto basesIt = basesRange.begin();
        // PYSIDE-1887, PYSIDE-86: Skip QObject base class of QGraphicsObject;
        // it needs to use always QGraphicsItem as a QVariant type for
        // QGraphicsItem::itemChange() to work.
        if (basesRange.size() > 1 && qstrcmp(typeName, "QGraphicsObject*") == 0) {
            PyTypeObject *firstBaseType = *basesIt;
            if (SbkObjectType_Check(firstBaseType)) {
                const char *firstBaseTypeName = Shiboken::ObjectType::getOriginalName(firstBaseType);
                if (firstBaseTypeName != nullptr && qstrcmp(firstBaseTypeName, "QObject*") == 0)
                    ++basesIt;
            }
        }
        for (auto basesEnd = basesRange.end(); basesIt != basesEnd; ++basesIt) {
            const QMetaType derived = resolveMetaType(*basesIt);
            if (derived.isValid())
                return derived;
        }
        return {};
    }
    if (type->tp_base != nullptr)
        return resolveMetaType(type->tp_base);
    return {};
}

std::optional<QVariantList> pyListToVariantList(PyObject *list)
{
    if (list == nullptr || PySequence_Check(list) == 0)
        return std::nullopt;
    const auto size = PySequence_Size(list);
    if (size < 0) { // Some infinite (I/O read) thing? - bail out
        PyErr_Clear();
        return std::nullopt;
    }
    if (size == 0)
        return QVariantList{};
    return pyListToVariantListHelper(list, size);
}

QVariant convertToVariantList(PyObject *list)
{
    const auto size = PySequence_Size(list);
    if (size < 0) { // Some infinite (I/O read) thing? - bail out
        PyErr_Clear();
        return {};
    }
    if (size == 0)
        return QVariantList{};

    if (isStringList(list)) {
        auto stringListO = listToStringList(list);
        if (stringListO.has_value())
            return {stringListO.value()};
    }

    if (QVariant valueList = convertToValueList(list); valueList.isValid())
        return valueList;

    if (auto vlO = pyListToVariantListHelper(list, size); vlO.has_value())
        return vlO.value();

    return {};
}

QVariant convertToVariantMap(PyObject *map)
{
    if (map == nullptr || PyDict_Check(map) == 0)
        return {};

    QVariantMap result;
    if (PyDict_Size(map) == 0)
        return result;

    Shiboken::AutoDecRef keys(PyDict_Keys(map));
    if (!isStringList(keys))
        return {};

    Shiboken::Conversions::SpecificConverter converter(qVariantTypeName);
    if (!converter) {
        warnConverter(qVariantTypeName);
        return {};
    }

    for (auto [key, value] : Shiboken::PyDictRange(map)) {
        QVariant cppValue;
        converter.toCpp(value, &cppValue);
        result.insert(PySide::pyUnicodeToQString(key), cppValue);
    }
    return result;
}

PyObject *javascriptVariantToPython(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: {
        if (value.toBool())
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }
    break;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return PyFloat_FromDouble(value.toDouble());
    default:
        break;
    }
    return PySide::qStringToPyUnicode(value.toString());
}

} // namespace PySide::Variant
