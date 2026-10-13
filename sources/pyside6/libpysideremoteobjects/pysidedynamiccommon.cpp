// Copyright (C) 2025 Ford Motor Company
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidedynamiccommon_p.h"
#include "pysidedynamicenum_p.h"

#include <sbkpep.h>
#include <sbkstring.h>

#include <QtCore/qmetaobject.h>

#ifdef Py_GIL_DISABLED
#  include <sbkftoptions.h>
#  include <sbkheldlocks.h>

#  include <mutex>
#endif

int capsule_count = 0;

#ifdef Py_GIL_DISABLED
// Not a raw lock: Qt may warn under it, which runs a Python message handler,
// and that may drop a dynamic type, whose deletion takes the lock again
static std::recursive_mutex &repRegistryMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

RepRegistryGuard::RepRegistryGuard()
{
    if (!Shiboken::FreeThreading::optionEnabled(Shiboken::FreeThreading::Option::RepRegistryLock))
        return;
    Shiboken::lockDetached(repRegistryMutex());
    m_locked = true;
}

RepRegistryGuard::~RepRegistryGuard()
{
    if (!m_locked)
        return;
    repRegistryMutex().unlock();
}
#endif // Py_GIL_DISABLED

using namespace Shiboken;

PyObject *toPython(const QVariant &variant)
{
    auto metaType = variant.metaType();
    if (!metaType.isValid()) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid meta type");
        return nullptr;
    }
    Conversions::SpecificConverter converter(metaType.name());
    auto *value = converter.toPython(variant.data());
    if (metaType.flags().testFlag(QMetaType::IsGadget)) {
        // A single converter is used for all POD types - it converts to a Python
        // tuple. We need an additional step to convert to our Python type for the POD.
        // Thankfully, the converter stores the specific type we created, so we can call
        // the constructor with the tuple.
        auto *podType = Conversions::getPythonTypeObject(converter);
        if (!podType) {
            Py_DECREF(value);
            PyErr_SetString(PyExc_RuntimeError, "Failed to get Python type for POD");
            return nullptr;
        }
        PyObject *podValue = PyObject_CallObject(reinterpret_cast<PyObject *>(podType), value);
        Py_DECREF(value);
        if (!podValue) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create POD instance");
            return nullptr;
        }
        return podValue;
    }
    if (metaType.flags().testFlag(QMetaType::IsEnumeration)) {
        // Enums are converted to Python ints
        auto *enumType = Conversions::getPythonTypeObject(converter);
        if (!enumType) {
            Py_DECREF(value);
            PyErr_SetString(PyExc_RuntimeError, "Failed to get Python type for enum");
            return nullptr;
        }
        PyObject *enumValue = PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject *>(enumType),
                                                           value, nullptr);
        Py_DECREF(value);
        if (!enumValue) {
            PyErr_Print();
            PyErr_SetString(PyExc_RuntimeError, "Failed to create enum instance");
            return nullptr;
        }
        return enumValue;
    }
    return value;
}

/*!
    @brief Creates and manages memory for Python enum types for each QEnum in the
    provided QMetaObject.

    This function iterates over the enumerators in the provided QMetaObject \a
    meta, creates corresponding Python enum types, and stores them in a
    dictionary. The dictionary is then set as an attribute ()"_enum_data") on
    the provided Python object, to be accessed by the _get_enum that has been
    added to each of our dynamic types.

    These are "managed" in the sense that the enums clean up their converters
    using our PyCapsule method, and by adding the dictionary as a Python
    attribute, the dictionary will be cleaned up when the containing type is
    garbage collected.

    It returns 0 on success, or -1 on failure.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a self A pointer to the Python object where the enum data will be stored.
            \li \a meta A pointer to the QMetaObject containing the enumerators.
    \endtable

    \inmodule libpysideremoteobjects
*/
int create_managed_py_enums(PyObject *self, QMetaObject *meta)
{
    PyObject *enum_data = PyDict_New();
    for (int i = meta->enumeratorOffset(); i < meta->enumeratorCount(); ++i) {
        auto metaEnum = meta->enumerator(i);
        auto *enumType = createEnumType(&metaEnum);
        if (!enumType) {
            PyErr_Print();
            PyErr_Format(PyExc_RuntimeError, "Failed to create enum type for POD '%s'",
                         meta->className());
            return -1;
        }
        PyDict_SetItemString(enum_data, metaEnum.enumName(),
                             reinterpret_cast<PyObject *>(enumType));
        Py_DECREF(enumType);
    }
    if (PyObject_SetAttrString(self, "_enum_data", enum_data) < 0) {
        PyErr_Print();
        qWarning() << "Failed to set _enum_data attribute on type"
                   << PepType_GetFullyQualifiedNameStr(reinterpret_cast<PyTypeObject *>(self));
        return -1;
    }
    Py_DECREF(enum_data);

    return 0;
}

PyObject *DynamicType_get_enum(PyObject *self, PyObject *name)
{
    // Our enum types are always stored in a dictionary attribute named "_enum_data"
    PyObject *enum_dict = PyObject_GetAttrString(self, "_enum_data");
    if (!enum_dict) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get _enum_data attribute");
        return nullptr;
    }

    PyObject *enum_type = PyDict_GetItem(enum_dict, name);
    Py_DECREF(enum_dict);

    if (!enum_type) {
        PyErr_Format(PyExc_KeyError, "Enum '%s' not found", String::toCString(name));
        return nullptr;
    }

    Py_INCREF(enum_type);
    return enum_type;
}
