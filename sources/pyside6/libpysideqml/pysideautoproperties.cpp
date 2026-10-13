// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideautoproperties.h"
#include "pyside_p.h"
#include "dynamicqmetaobject.h"
#include "pysideqobject.h"

#include <autodecref.h>
#include <pep384ext.h>
#include <sbkpython.h>
#include <sbktypefactory.h>
#include <signature.h>
#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#  include <sbkfailpoint.h>
#  include <sbkftoptions.h>

#  include <atomic>
#endif


using namespace Shiboken;

// Python helper module (loaded lazily)
#ifdef Py_GIL_DISABLED
// Two threads importing the helper first: the first published is kept
static PyObject *getHelperModule()
{
    static std::atomic<PyObject *> helperModule{};
    PyObject *published = helperModule.load(std::memory_order_acquire);
    if (published != nullptr)
        return published;
    PyObject *candidate = PyImport_ImportModule("PySide6.support.auto_property_helper");
    Shiboken::failpoint("qml-helper-before-publish");
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::QmlHelperModuleOnce)) {
        helperModule.store(candidate, std::memory_order_release);
        return candidate;
    }
    if (candidate == nullptr || helperModule.compare_exchange_strong(
            published, candidate, std::memory_order_acq_rel)) {
        return candidate;
    }
    Py_DECREF(candidate);
    return published;
}
#else // Py_GIL_DISABLED
static PyObject *getHelperModule()
{
    // GIL is held by all callers (Python C API callbacks) so no extra lock needed.
    static PyObject *helperModule = nullptr;
    if (!helperModule)
        helperModule = PyImport_ImportModule("PySide6.support.auto_property_helper");
    return helperModule;
}
#endif // Py_GIL_DISABLED

static PyObject *callHelperFunction(const char *funcName, PyObject *arg)
{
    PyObject *mod = getHelperModule();
    if (!mod)
        return nullptr;
    AutoDecRef func(PyObject_GetAttrString(mod, funcName));
    if (func.isNull())
        return nullptr;
    return PyObject_CallFunctionObjArgs(func, arg, nullptr);
}

// Rebuild the QMetaObject for a QObject derived type after augmentation
static bool rebuildMetaObject(PyTypeObject *type)
{
    auto *userData = PySide::retrieveTypeUserData(type);
    if (!userData)
        return false;

    userData->mo.reparseType(type);
    userData->mo.update();
    return true;
}

// Augment a QObject derived class and rebuild its QMetaObject
namespace PySide::AutoProperties {

PyObject *augmentClass(PyObject *klass)
{
    auto *klassType = reinterpret_cast<PyTypeObject *>(klass);
    if (!PySide::isQObjectDerived(klassType, false)) {
        PyErr_SetString(PyExc_TypeError,
                        "@auto_properties requires a QObject derived class");
        return nullptr;
    }

    // for duplicate use of @auto_properties
    AutoDecRef alreadyKey(PyUnicode_FromString("_pyside_auto_props_applied"));

    if (alreadyKey.isNull())
        return nullptr;
    AutoDecRef alreadyDone(PyObject_GetAttr(klass, alreadyKey));
    if (!alreadyDone.isNull()) {
        const int truthy = PyObject_IsTrue(alreadyDone);
        if (truthy < 0)
            return nullptr; // propagate exception
        if (truthy) {
            Py_INCREF(klass);
            return klass;
        }
    } else {
        PyErr_Clear(); // clear expected AttributeError
    }

    AutoDecRef result(callHelperFunction("augment_class", klass));
    if (result.isNull())
        return nullptr;

    if (!rebuildMetaObject(klassType)) {
        PyErr_WarnFormat(PyExc_RuntimeWarning, 1,
                         "auto_properties: could not rebuild QMetaObject "
                         "for %s - are you sure this is a QObject subclass "
                         "that has been properly initialized?",
                         PepExt_TypeGetQualName(klassType));
        PyErr_Clear();
    }

    Py_INCREF(klass);
    return klass;
}

} // namespace PySide::AutoProperties

struct PySideAutoProperties
{
    PyObject_HEAD
};

extern "C"
{

static PyObject *autoPropertiesTpNew(PyTypeObject * /* subtype */,
                                     PyObject *args, PyObject * /* kwds */)
{
    PyObject *klass = nullptr;
    if (!PyArg_UnpackTuple(args, "auto_properties", 1, 1, &klass))
        return nullptr;
    if (!PyType_Check(klass)) {
        PyErr_SetString(PyExc_TypeError,
                        "@auto_properties must decorate a class, not an instance");
        return nullptr;
    }
    return PySide::AutoProperties::augmentClass(klass);
}

static void autoPropertiesTpDealloc(PyObject *self)
{
    Py_DECREF(Py_TYPE(self));
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createAutoPropertiesType()
{
    PyType_Slot PySideAutoPropertiesType_slots[] = {
        {Py_tp_new, reinterpret_cast<void *>(autoPropertiesTpNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(autoPropertiesTpDealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideAutoPropertiesType_spec = {
        "2:PySide6.QtQmlFeatures.auto_properties",
        sizeof(PySideAutoProperties),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideAutoPropertiesType_slots,
    };

    return SbkType_FromSpec(&PySideAutoPropertiesType_spec);
}

static PyTypeObject *PySideAutoProperties_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createAutoPropertiesType);
#else
    static auto *type = createAutoPropertiesType();
#endif
    return type;
}

} // extern "C"

/*!
    \namespace PySide::AutoProperties
    \brief Namespace for the \c @auto_properties class decorator of QtQmlFeatures.
    \ingroup QmlFeatures
    \inmodule libpysideqml
*/
namespace PySide::AutoProperties {

static const char *AutoProperties_SignatureStrings[] = {
    "PySide6.QtQmlFeatures.auto_properties(self,klass:type)->None",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *autoPropertiesType = PySideAutoProperties_TypeF();
    if (InitSignatureStrings(autoPropertiesType,
                             AutoProperties_SignatureStrings) < 0) {
        return;
    }
    auto *obType = reinterpret_cast<PyObject *>(autoPropertiesType);
    Py_INCREF(obType);
    PepModule_AddType(module, autoPropertiesType);
}

} // namespace PySide::AutoProperties
