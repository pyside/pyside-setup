// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideeffect_p.h"

#include <autodecref.h>
#include <pep384ext.h>
#include <sbkpython.h>
#include <sbkpep.h>
#include <sbkpytuplerange.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Shiboken;

struct PySideEffect
{
    PyObject_HEAD
    PyObject *propertyNames; // list[str]. property names to observe
};

extern "C"
{

static int effectTpInit(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    const Shiboken::PyTupleEnumeratedRange argsRange(args);
    if (argsRange.isEmpty()) {
        PyErr_SetString(PyExc_TypeError,
                        "@effect requires at least one property name");
        return -1;
    }

    auto *data = reinterpret_cast<PySideEffect *>(self);
    Py_XDECREF(data->propertyNames);
    data->propertyNames = PyList_New(argsRange.size());
    if (!data->propertyNames)
        return -1;

    for (auto [i, arg] : argsRange) {
        if (!PyUnicode_Check(arg)) {
            PyErr_Format(PyExc_TypeError,
                         "@effect argument %zd must be a string, not %s",
                         i + 1, Py_TYPE(arg)->tp_name);
            Py_CLEAR(data->propertyNames);
            return -1;
        }
        Py_INCREF(arg);
        PyList_SetItem(data->propertyNames, i, arg);
    }
    return 0;
}

static PyObject *effectCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *callback = nullptr;
    if (!PyArg_UnpackTuple(args, "effect.__call__", 1, 1, &callback))
        return nullptr;

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError,
                        "@effect can only decorate callable objects");
        return nullptr;
    }

    Py_INCREF(callback);

    auto *data = reinterpret_cast<PySideEffect *>(self);
    PyObject *attrName = PySide::Effect::effectAttrName();

    // Get or create the effect list on the callback
    PyObject *existing = PyObject_GetAttr(callback, attrName);
    if (existing && PyList_Check(existing)) {
        // Extend with new property names
        Py_ssize_t n = PyList_Size(data->propertyNames);
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *item = PyList_GetItem(data->propertyNames, i);
            if (PyList_Append(existing, item) < 0) {
                Py_DECREF(existing);
                Py_DECREF(callback);
                return nullptr;
            }
        }
        Py_DECREF(existing);
    } else {
        Py_XDECREF(existing); // release any non-list object returned by GetAttr
        PyErr_Clear();
        AutoDecRef copy(PyList_GetSlice(data->propertyNames, 0,
                                        PyList_Size(data->propertyNames)));
        if (copy.isNull()) {
            Py_DECREF(callback);
            return nullptr;
        }
        if (PyObject_SetAttr(callback, attrName, copy) < 0) {
            Py_DECREF(callback);
            return nullptr;
        }
    }

    return callback;
}

static void effectTpDealloc(PyObject *self)
{
    auto *data = reinterpret_cast<PySideEffect *>(self);
    Py_XDECREF(data->propertyNames);
    Py_DECREF(Py_TYPE(self));
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createEffectType()
{
    PyType_Slot PySideEffectType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(effectCall)},
        {Py_tp_init, reinterpret_cast<void *>(effectTpInit)},
        {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(effectTpDealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideEffectType_spec = {
        "2:PySide6.QtQmlFeatures.effect",
        sizeof(PySideEffect),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideEffectType_slots,
    };

    return SbkType_FromSpec(&PySideEffectType_spec);
}

static PyTypeObject *PySideEffect_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createEffectType);
#else
    static auto *type = createEffectType();
#endif
    return type;
}

} // extern "C"

namespace PySide::Effect {

PyObject *effectAttrName()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString s("_pyside_effect");
#else
    static PyObject *const s = Shiboken::String::createStaticString("_pyside_effect");
#endif
    return s;
}

static const char *Effect_SignatureStrings[] = {
    "PySide6.QtQmlFeatures.effect(self,*property_names:str)",
    "PySide6.QtQmlFeatures.effect.__call__(self,function:typing.Callable)->typing.Callable",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *effectType = PySideEffect_TypeF();
    if (InitSignatureStrings(effectType, Effect_SignatureStrings) < 0)
        return;
    auto *obEffectType = reinterpret_cast<PyObject *>(effectType);
    Py_INCREF(obEffectType);
    PepModule_AddType(module, effectType);
}

} // namespace PySide::Effect
