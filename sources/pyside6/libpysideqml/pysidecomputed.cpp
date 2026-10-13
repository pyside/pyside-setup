// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidecomputed_p.h"

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

struct PySideComputed
{
    PyObject_HEAD
    PyObject *depNames; // list[str]. dependency property names
};

extern "C"
{

static int computedTpInit(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    const Shiboken::PyTupleEnumeratedRange argsRange(args);
    if (argsRange.isEmpty()) {
        PyErr_SetString(PyExc_TypeError,
                        "@computed requires at least one dependency name");
        return -1;
    }

    auto *data = reinterpret_cast<PySideComputed *>(self);
    Py_XDECREF(data->depNames);
    data->depNames = PyList_New(argsRange.size());
    if (!data->depNames)
        return -1;

    for (auto [i, arg] : argsRange) {
        if (!PyUnicode_Check(arg)) {
            PyErr_Format(PyExc_TypeError,
                         "@computed argument %zd must be a string, not %s",
                         i + 1, Py_TYPE(arg)->tp_name);
            Py_CLEAR(data->depNames);
            return -1;
        }
        Py_INCREF(arg);
        PyList_SetItem(data->depNames, i, arg);
    }
    return 0;
}

static PyObject *computedCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *callback = nullptr;
    if (!PyArg_UnpackTuple(args, "computed.__call__", 1, 1, &callback))
        return nullptr;

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError,
                        "@computed can only decorate callable objects");
        return nullptr;
    }

    Py_INCREF(callback);

    auto *data = reinterpret_cast<PySideComputed *>(self);
    PyObject *attrName = PySide::Computed::computedAttrName();

    // Store the dependency list directly on the callback as _pyside_computed,
    // mirroring how _pyside_watch stores its list on @watch-decorated methods.
    AutoDecRef depsCopy(PyList_GetSlice(data->depNames, 0,
                                        PyList_Size(data->depNames)));
    if (depsCopy.isNull()) {
        Py_DECREF(callback);
        return nullptr;
    }
    if (PyObject_SetAttr(callback, attrName, depsCopy) < 0) {
        Py_DECREF(callback);
        return nullptr;
    }

    return callback;
}

static void computedTpDealloc(PyObject *self)
{
    auto *data = reinterpret_cast<PySideComputed *>(self);
    Py_XDECREF(data->depNames);
    Py_DECREF(Py_TYPE(self));
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createComputedType()
{
    PyType_Slot PySideComputedType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(computedCall)},
        {Py_tp_init, reinterpret_cast<void *>(computedTpInit)},
        {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(computedTpDealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideComputedType_spec = {
        "2:PySide6.QtQmlFeatures.computed",
        sizeof(PySideComputed),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideComputedType_slots,
    };

    return SbkType_FromSpec(&PySideComputedType_spec);
}

static PyTypeObject *PySideComputed_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createComputedType);
#else
    static auto *type = createComputedType();
#endif
    return type;
}

} // extern "C"

namespace PySide::Computed {

PyObject *computedAttrName()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString s("_pyside_computed");
#else
    static PyObject *const s = Shiboken::String::createStaticString("_pyside_computed");
#endif
    return s;
}

static const char *Computed_SignatureStrings[] = {
    "PySide6.QtQmlFeatures.computed(self,*dep_names:str)",
    "PySide6.QtQmlFeatures.computed.__call__(self,function:typing.Callable)->typing.Callable",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *computedType = PySideComputed_TypeF();
    if (InitSignatureStrings(computedType, Computed_SignatureStrings) < 0)
        return;
    auto *obComputedType = reinterpret_cast<PyObject *>(computedType);
    Py_INCREF(obComputedType);
    PepModule_AddType(module, computedType);
}

} // namespace PySide::Computed
