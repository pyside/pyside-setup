// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmlcomponent.h"

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

// Python helper module
#ifdef Py_GIL_DISABLED
// Two threads importing the helper first: the first published is kept.
static PyObject *getQmlComponentHelperModule()
{
    static std::atomic<PyObject *> helperModule{};
    PyObject *published = helperModule.load(std::memory_order_acquire);
    if (published != nullptr)
        return published;
    PyObject *candidate = PyImport_ImportModule("PySide6.support.qml_component_helper");
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
static PyObject *getQmlComponentHelperModule()
{
    // The GIL is held by all callers (Python C API path).
    static PyObject *helperModule = nullptr;
    if (!helperModule) {
        helperModule =
            PyImport_ImportModule("PySide6.support.qml_component_helper");
    }
    return helperModule;
}
#endif // Py_GIL_DISABLED

struct PySideQmlComponent
{
    PyObject_HEAD
};

extern "C"
{

static PyObject *qmlComponentTpNew(PyTypeObject * /* subtype */,
                                   PyObject *args, PyObject *kwds)
{
    PyObject *mod = getQmlComponentHelperModule();
    if (!mod)
        return nullptr;
    AutoDecRef func(PyObject_GetAttrString(mod, "create_factory"));
    if (func.isNull())
        return nullptr;
    // Forward args/kwds verbatim. This C call adds no Python frame, so
    // the helper's inspect.stack()[1] sees the user's calling frame and
    // can resolve relative .qml paths against it.
    return PyObject_Call(func, args, kwds);
}

static void qmlComponentTpDealloc(PyObject *self)
{
    Py_DECREF(Py_TYPE(self));
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createQmlComponentType()
{
    PyType_Slot PySideQmlComponentType_slots[] = {
        {Py_tp_new, reinterpret_cast<void *>(qmlComponentTpNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(qmlComponentTpDealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideQmlComponentType_spec = {
        "2:PySide6.QtQmlFeatures.load_qml_component",
        sizeof(PySideQmlComponent),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideQmlComponentType_slots,
    };

    return SbkType_FromSpec(&PySideQmlComponentType_spec);
}

static PyTypeObject *PySideQmlComponent_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createQmlComponentType);
#else
    static auto *type = createQmlComponentType();
#endif
    return type;
}

} // extern "C"

/*!
    \namespace PySide::QmlComponent
    \brief Namespace for the \c load_qml_component function of QtQmlFeatures.
    \ingroup QmlFeatures
    \inmodule libpysideqml
*/
namespace PySide::QmlComponent {

static const char *QmlComponent_SignatureStrings[] = {
    "PySide6.QtQmlFeatures.load_qml_component(self,engine:PySide6.QtQml.QQmlEngine,"
    "source:str=...,module:str=...,type_name:str=...)",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *qmlComponentType = PySideQmlComponent_TypeF();
    if (InitSignatureStrings(qmlComponentType,
                             QmlComponent_SignatureStrings) < 0) {
        return;
    }
    auto *obType = reinterpret_cast<PyObject *>(qmlComponentType);
    Py_INCREF(obType);
    PepModule_AddType(module, qmlComponentType);
}

} // namespace PySide::QmlComponent
