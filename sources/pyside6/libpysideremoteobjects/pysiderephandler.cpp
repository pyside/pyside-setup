// Copyright (C) 2025 Ford Motor Company
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysiderephandler_p.h"
#include "pysidedynamicclass_p.h"
#include "pysidedynamicpod_p.h"
#include "pysidedynamiccommon_p.h"

#include <pep384ext.h>
#include <sbkerrors.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <pysideutils.h>

#include <QtCore/qbuffer.h>
#include <QtCore/qiodevice.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qstringlist.h>

#include <QtRemoteObjects/qremoteobjectreplica.h>
#include <QtRemoteObjects/qremoteobjectpendingcall.h>

#include <private/qremoteobjectrepparser_p.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Qt::StringLiterals;
using namespace Shiboken;

/*!
    \group pysiderephandler
    \title PySideRepFile code
    \inmodule libpysideremoteobjects

    \brief This file contains the implementation of the PySideRepFile type and its
           associated methods for handling Qt Remote Objects in PySide6.

    \section1

    The PySideRepFile type provides functionality to parse and handle Qt Remote Objects
    (QtRO) files, and dynamically generate Python types for QtRO sources, replicas, and
    PODs (Plain Old Data structures).
    The RepFile_tp_methods array defines the methods available on the PySideRepFile object:

   \list
       \li source: Generates a dynamic Python type for a QtRO source class.
       \li replica: Generates a dynamic Python type for a QtRO replica class.
       \li pod: Generates a dynamic Python type for a QtRO POD class.
    \endlist

    When generating a source or replica type, the generateDynamicType function is
    used, creating a new Python type based on the generated QMetaObject, and adds
    method descriptors for the required methods. A QVariantList for the types
    properties is also created, populated with default values if set in the input
    \c .rep file.
*/

struct PySideRepFilePrivate
{
    AST ast;
    PyObject *podDict{};
    PyObject *replicaDict{};
    PyObject *sourceDict{};
    QStringList classes;
    QStringList pods;
};

static QVariantList generateProperties(QMetaObject *meta, const ASTClass &astClass);

extern "C"
{
static PyObject *get_capsule_count()
{
    return PyLong_FromLong(capsule_count);
}

// Code for the PySideRepFile type
static PyObject *RepFile_tp_string(PyObject *self);
static PyObject *RepFile_tp_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds);
static int RepFile_tp_init(PyObject *self, PyObject *args, PyObject *kwds);
static void RepFile_tp_free(void *self);
static void RepFile_tp_dealloc(PySideRepFile *self);

static PyObject *RepFile_get_pods(PySideRepFile *self, void * /*unused*/);
static PyObject *RepFile_get_replicas(PySideRepFile *self, void * /*unused*/);
static PyObject *RepFile_get_sources(PySideRepFile *self, void * /*unused*/);

bool instantiateFromDefaultValue(QVariant &variant, const QString &defaultValue);

static PyObject *cppToPython_POD_Tuple(const void *cppIn);
static void pythonToCpp_Tuple_POD(PyObject *pyIn, void *cppOut);
static PythonToCppFunc is_Tuple_PythonToCpp_POD_Convertible(PyObject *pyIn);

static PyGetSetDef RepFile_tp_getters[] = {
    {"pod", reinterpret_cast<getter>(RepFile_get_pods), nullptr, "POD dictionary", nullptr},
    {"replica", reinterpret_cast<getter>(RepFile_get_replicas), nullptr, "Replica dictionary", nullptr},
    {"source", reinterpret_cast<getter>(RepFile_get_sources), nullptr, "Source dictionary", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}  // Sentinel
};

static PyTypeObject *createRepFileType()
{
    PyType_Slot PySideRepFileType_slots[] = {
        {Py_tp_str,         reinterpret_cast<void *>(RepFile_tp_string)},
        {Py_tp_init,        reinterpret_cast<void *>(RepFile_tp_init)},
        {Py_tp_new,         reinterpret_cast<void *>(RepFile_tp_new)},
        {Py_tp_free,        reinterpret_cast<void *>(RepFile_tp_free)},
        {Py_tp_dealloc,     reinterpret_cast<void *>(RepFile_tp_dealloc)},
        {Py_tp_getset,      reinterpret_cast<void *>(RepFile_tp_getters)},
        {0, nullptr}
    };

    PyType_Spec PySideRepFileType_spec = {
        "2:PySide6.QtRemoteObjects.RepFile",
        sizeof(PySideRepFile),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideRepFileType_slots};
    return SbkType_FromSpec(&PySideRepFileType_spec);
}

PyTypeObject *PySideRepFile_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createRepFileType);
#else
    static auto *type = createRepFileType();
#endif
    return type;
}

static PyObject *RepFile_tp_string(PyObject *self)
{
    auto *cppSelf = reinterpret_cast<PySideRepFile *>(self);
    QString result = QStringLiteral("RepFile(Classes: [%1], PODs: [%2])")
                        .arg(cppSelf->d->classes.join(", "_L1), cppSelf->d->pods.join(", "_L1));
    return PyUnicode_FromString(result.toUtf8().constData());
}

static PyObject *RepFile_tp_new(PyTypeObject *subtype, PyObject * /* args */, PyObject * /* kwds */)
{
    auto *me = PepExt_TypeCallAlloc<PySideRepFile>(subtype, 0);
    auto *priv = new PySideRepFilePrivate;
    priv->podDict = PyDict_New();
    if (!priv->podDict) {
        delete priv;
        return nullptr;
    }
    priv->replicaDict = PyDict_New();
    if (!priv->replicaDict) {
        Py_DECREF(priv->podDict);
        delete priv;
        return nullptr;
    }
    priv->sourceDict = PyDict_New();
    if (!priv->sourceDict) {
        Py_DECREF(priv->podDict);
        Py_DECREF(priv->replicaDict);
        delete priv;
        return nullptr;
    }
    me->d = priv;
    return reinterpret_cast<PyObject *>(me);
}

static PyObject *RepFile_get_pods(PySideRepFile *self, void * /* closure */)
{
    Py_INCREF(self->d->podDict);
    return self->d->podDict;
}

static PyObject *RepFile_get_replicas(PySideRepFile *self, void * /* closure */)
{
    Py_INCREF(self->d->replicaDict);
    return self->d->replicaDict;
}

static PyObject *RepFile_get_sources(PySideRepFile *self, void * /* closure */)
{
    Py_INCREF(self->d->sourceDict);
    return self->d->sourceDict;
}

static void RepFile_tp_dealloc(PySideRepFile *self)
{
    Py_XDECREF(self->d->podDict);
    Py_XDECREF(self->d->replicaDict);
    Py_XDECREF(self->d->sourceDict);
    PepExt_TypeCallFree(reinterpret_cast<PyObject *>(self));
}

static int parseArgsToAST(PyObject *args, PySideRepFile *repFile)
{
    // Verify args is a single string argument
    if (PyTuple_Size(args) != 1 || !PyUnicode_Check(PyTuple_GetItem(args, 0))) {
        PyErr_SetString(PyExc_TypeError, "RepFile constructor requires a single string argument");
        return -1;
    }

    // Wrap contents into a QBuffer
    const auto contents = PySide::pyStringToQString(PyTuple_GetItem(args, 0));
    auto byteArray = contents.toUtf8();
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::ReadOnly);
    RepParser repparser(buffer);
    if (!repparser.parse()) {
        PyErr_Format(PyExc_RuntimeError, "Error parsing input, line %d: error: %s",
                     repparser.lineNumber(), qPrintable(repparser.errorString()));
        auto lines = contents.split("\n"_L1);
        auto lMin = std::max(1, repparser.lineNumber() - 2);
        auto lMax = std::min(repparser.lineNumber() + 2, int(lines.size()));
        // Print a few lines around the error
        qWarning() << "Contents:";
        for (int i = lMin; i <= lMax; ++i) {
            if (i == repparser.lineNumber())
                qWarning().nospace() << " line " << i << ": > " << lines.at(i - 1);
            else
                qWarning().nospace() << " line " << i << ":   " << lines.at(i - 1);
        }
        return -1;
    }

    repFile->d->ast = repparser.ast();

    return 0;
}

#ifdef Py_GIL_DISABLED
// a type is made in Qt's registry of QtRemoteObjects, see RepRegistryGuard
extern "C++" {
template <class Make>
static QMetaObject *registered(Make make)
{
    RepRegistryGuard guard;
    return make();
}
} // extern "C++"
#endif

static const char *repName(QMetaObject *meta)
{
    const int ind = meta->indexOfClassInfo(QCLASSINFO_REMOTEOBJECT_TYPE);
    return ind >= 0 ? meta->classInfo(ind).value() : "<Invalid RemoteObject>";
}

static int RepFile_tp_init(PyObject *self, PyObject *args, PyObject * /* kwds */)
{
    auto *cppSelf = reinterpret_cast<PySideRepFile *>(self);
    if (parseArgsToAST(args, cppSelf) < 0)
        return -1;

    for (const auto &pod : std::as_const(cppSelf->d->ast.pods)) {
        cppSelf->d->pods << pod.name;
        auto *qobject = new QObject;
#ifdef Py_GIL_DISABLED
        auto *meta = registered([&] { return createAndRegisterMetaTypeFromPOD(pod, qobject); });
#else
        auto *meta = createAndRegisterMetaTypeFromPOD(pod, qobject);
#endif
        if (!meta) {
            deleteTrackedObject(qobject);
            PyErr_Format(PyExc_RuntimeError, "Failed to create meta object for POD '%s'",
                         pod.name.toUtf8().constData());
            return -1;
        }

        PyTypeObject *newType = createPodType(meta);
        if (!newType) {
            deleteTrackedObject(qobject);
            PyErr_Print();
            PyErr_Format(PyExc_RuntimeError, "Failed to create POD type for POD '%s'",
                         pod.name.toUtf8().constData());
            return -1;
        }
        if (set_cleanup_capsule_attr_for_pointer(newType, "_qobject_capsule", qobject) < 0) {
            deleteTrackedObject(qobject);
            return -1;
        }

        PyDict_SetItemString(cppSelf->d->podDict, meta->className(),
                             reinterpret_cast<PyObject *>(newType));
        Py_DECREF(newType);
    }

    if (PyErr_Occurred())
        PyErr_Print();

    for (const auto &cls : std::as_const(cppSelf->d->ast.classes)) {
        cppSelf->d->classes << cls.name;

        // Create Source type
        {
            auto *qobject = new QObject;
#ifdef Py_GIL_DISABLED
            auto *meta = registered([&] {
                return createAndRegisterSourceFromASTClass(cls, qobject);
            });
#else
            auto *meta = createAndRegisterSourceFromASTClass(cls, qobject);
#endif
            if (!meta) {
                deleteTrackedObject(qobject);
                PyErr_Format(PyExc_RuntimeError, "Failed to create Source meta object for class '%s'",
                             cls.name.toUtf8().constData());
                return -1;
            }

            auto properties = generateProperties(meta, cls);
            // Check if an error occurred during generateProperties
            if (PyErr_Occurred()) {
                deleteTrackedObject(qobject);
                return -1;
            }
            auto *propertiesPtr = new QVariantList(properties);
            auto *pyCapsule = PyCapsule_New(propertiesPtr, nullptr, [](PyObject *capsule) {
                delete reinterpret_cast<QVariantList *>(PyCapsule_GetPointer(capsule, nullptr));
            });

            PyTypeObject *newType = createDynamicClass(meta, pyCapsule);
            if (!newType) {
                deleteTrackedObject(qobject);
                PyErr_Format(PyExc_RuntimeError,
                             "Failed to create Source Python type for class '%s'",
                             meta->className());
                return -1;
            }
            if (set_cleanup_capsule_attr_for_pointer(newType, "_qobject_capsule", qobject) < 0) {
                deleteTrackedObject(qobject);
                return -1;
            }

            PyDict_SetItemString(cppSelf->d->sourceDict, repName(meta),
                                 reinterpret_cast<PyObject *>(newType));
            Py_DECREF(newType);
        }

        // Create Replica type
        {
            auto *qobject = new QObject;
#ifdef Py_GIL_DISABLED
            auto *meta = registered([&] {
                return createAndRegisterReplicaFromASTClass(cls, qobject);
            });
#else
            auto *meta = createAndRegisterReplicaFromASTClass(cls, qobject);
#endif
            if (!meta) {
                deleteTrackedObject(qobject);
                PyErr_Format(PyExc_RuntimeError,
                             "Failed to create Replica meta object for class '%s'",
                             qPrintable(cls.name));
                return -1;
            }

            auto properties = generateProperties(meta, cls);
            // Check if an error occurred during generateProperties
            if (PyErr_Occurred()) {
                deleteTrackedObject(qobject);
                return -1;
            }
            auto *propertiesPtr = new QVariantList(properties);
            auto *pyCapsule = PyCapsule_New(propertiesPtr, nullptr, [](PyObject *capsule) {
                delete reinterpret_cast<QVariantList *>(PyCapsule_GetPointer(capsule, nullptr));
            });

            PyTypeObject *newType = createDynamicClass(meta, pyCapsule);
            if (!newType) {
                deleteTrackedObject(qobject);
                PyErr_Format(PyExc_RuntimeError,
                             "Failed to create Replica Python type for class '%s'",
                             meta->className());
                return -1;
            }
            if (set_cleanup_capsule_attr_for_pointer(newType, "_qobject_capsule", qobject) < 0) {
                deleteTrackedObject(qobject);
                return -1;
            }

            PyDict_SetItemString(cppSelf->d->replicaDict, repName(meta),
                                 reinterpret_cast<PyObject *>(newType));
            Py_DECREF(newType);
        }
    }

    return 0;
}

static void RepFile_tp_free(void *self)
{
    auto *obj = reinterpret_cast<PySideRepFile*>(self);
    delete obj->d;
}

/*!
    \brief Sets the QVariant value based on the provided default value text.

    This function attempts to set the provided QVariant's \a variant value based on
    the provided text \a defaultValue. It evaluates the text as a Python expression,
    the python type associated with the provided QMetaType. It first retrieves the Python
    type object corresponding to the given QMetaType, then constructs a Python
    expression to instantiate the type with the default value. The expression is
    evaluated using PyRun_String, and the result is then set on the QVariant.

    \note The \a variant is passed by reference and modified in place.

    It returns \c true if the instantiation is successful, \c false otherwise.

    \ingroup pysiderephandler
 */
bool instantiateFromDefaultValue(QVariant &variant, const QString &defaultValue)
{
    auto metaType = variant.metaType();
    if (!metaType.isValid()) {
        PyErr_Format(PyExc_TypeError, "Invalid meta type");
        return false;
    }
    auto *pyType = Shiboken::Conversions::getPythonTypeObject(metaType.name());
    if (!pyType) {
        PyErr_Format(PyExc_TypeError, "Failed to find Python type for meta type: %s",
                     metaType.name());
        return false;
    }

    // Evaluate the code
    static PyObject *pyLocals = PyDict_New();

    // Create the Python expression to evaluate
    std::string code = std::string(PepType_GetFullyQualifiedNameStr(pyType)) + '('
                       + defaultValue.toUtf8().constData() + ')';
    PyObject *pyResult = PyRun_String(code.c_str(), Py_eval_input, pyLocals, pyLocals);

    if (!pyResult) {
        Shiboken::Errors::Stash errorStash;
        PyErr_Format(PyExc_TypeError,
                     "Failed to generate default value. Error: %s. Problematic code: %s",
                     Shiboken::String::toCString(PyObject_Str(errorStash.getException())), code.c_str());
        errorStash.release();
        Py_DECREF(pyLocals);
        return false;
    }

    Conversions::SpecificConverter converter(metaType.name());
    if (!converter) {
        PyErr_Format(PyExc_TypeError, "Failed to find converter from Python type: %s to Qt type: %s",
                     pyResult->ob_type->tp_name, metaType.name());
        Py_DECREF(pyResult);
        return false;
    }
    converter.toCpp(pyResult, variant.data());
    Py_DECREF(pyResult);

    return true;
}

} // extern "C"

static QVariantList generateProperties(QMetaObject *meta, const ASTClass &astClass)
{
    QVariantList properties;
    auto propertyCount = astClass.properties.size();
    properties.reserve(propertyCount);
    for (auto i = 0; i < propertyCount; ++i) {
        auto j = i + meta->propertyOffset();  // Corresponding property index in the meta object
        auto metaProperty = meta->property(j);
        auto metaType = metaProperty.metaType();
        if (!metaType.isValid()) {
            PyErr_Format(PyExc_RuntimeError, "Invalid meta type for property %d: %s", i,
                         astClass.properties[i].type.toUtf8().constData());
            return {};
        }
        auto variant = QVariant(metaType);
        if (auto defaultValue = astClass.properties[i].defaultValue; !defaultValue.isEmpty()) {
            auto success = instantiateFromDefaultValue(variant, defaultValue);
            if (!success) {
                // Print a warning giving the property name, then propagate the error
                qWarning() << "Failed to instantiate default value for property: "
                           << metaProperty.name();
                return {};
            }
        }
        properties << variant;
    }
    return properties;
}

/*!
    \module libpysideremoteobjects
    \title PySide Remote Objects library
    \brief Library for implementing Qt Remote Objects functionality.

    \section1 Reference

    \list
    \li \l{PySideRepFile code}
    \li \l{Capsule code}
    \endlist
*/

// qdoc dummy.
/*!
    \namespace PySide
    \brief Root namespace.
    \inmodule libpysideremoteobjects
*/

/*!
    \namespace PySide::RemoteObjects
    \brief Root namespace for code that is not extern "C".
    \inmodule libpysideremoteobjects
*/
namespace PySide::RemoteObjects
{

static const char *RepFile_SignatureStrings[] = {
    "PySide6.RemoteObjects.RepFile(self,content:str)",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    if (InitSignatureStrings(PySideRepFile_TypeF(), RepFile_SignatureStrings) < 0)
        return;

    qRegisterMetaType<QRemoteObjectPendingCall>();
    qRegisterMetaType<QRemoteObjectPendingCallWatcher>();

    auto *repType = PySideRepFile_TypeF();
    auto *obRepType = reinterpret_cast<PyObject *>(repType);
    Py_INCREF(obRepType);
    PepModule_AddType(module, repType);

    // Add a test helper to verify type reference counting
QT_WARNING_PUSH
QT_WARNING_DISABLE_CLANG("-Wcast-function-type-mismatch")
    static PyMethodDef get_capsule_count_def = {
        "getCapsuleCount",                                 // name of the function in Python
        reinterpret_cast<PyCFunction>(get_capsule_count),  // C function pointer
        METH_NOARGS,                                       // flags indicating parameters
        "Returns the current count of PyCapsule objects"   // docstring
    };
QT_WARNING_POP

    PepModule_Add(module, "getCapsuleCount", PyCFunction_New(&get_capsule_count_def, nullptr));
}

} // namespace PySide::RemoteObjects
