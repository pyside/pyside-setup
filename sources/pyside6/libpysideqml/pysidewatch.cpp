// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidewatch_p.h"

#include <autodecref.h>
#include <pep384ext.h>
#include <sbkpython.h>
#include <sbkpep.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Shiboken;

struct PySideWatch
{
    PyObject_HEAD
    PyObject *propertyName; // str — the single property name to watch
};

extern "C"
{

static int watchTpInit(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    const char *propName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Watch", &propName))
        return -1;

    auto *data = reinterpret_cast<PySideWatch *>(self);
    Py_XDECREF(data->propertyName);
    data->propertyName = PyUnicode_FromString(propName);
    if (!data->propertyName)
        return -1;
    return 0;
}


// currently only passive implementation and only stores the watched property names
// once @auto_properties is implemented, this will be extended to also support active watching of
// property changes and calling the decorated function i.e. callback
static PyObject *watchCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *callback = nullptr;
    if (!PyArg_UnpackTuple(args, "Watch.__call__", 1, 1, &callback))
        return nullptr;

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError,
                        "@watch can only decorate callable objects");
        return nullptr;
    }

    Py_INCREF(callback);

    // Validate that the callback accepts at least 2 positional params (self, change)
    // Use __code__.co_argcount which works for regular Python functions.
    {
        AutoDecRef codeAttrName(PyUnicode_FromString("__code__"));
        PyObject *code = PyObject_GetAttr(callback, codeAttrName);
        if (code) {
            AutoDecRef codeRef(code);
            AutoDecRef argcount(PyObject_GetAttrString(code, "co_argcount"));
            if (!argcount.isNull() && PyLong_Check(argcount)) {
                long nargs = PyLong_AsLong(argcount);
                if (nargs < 2) {
                    PyErr_Format(PyExc_TypeError,
                                 "@watch-decorated method must accept at least "
                                 "one parameter in addition to 'self': "
                                 "the 'change: Change' argument");
                    Py_DECREF(callback);
                    return nullptr;
                }
            }
        }
        PyErr_Clear(); // clear any error (e.g. no __code__ for bound methods)
    }

    auto *data = reinterpret_cast<PySideWatch *>(self);
    PyObject *attrName = PySide::Watch::watchAttrName();

    // Get or create the watch list on the callback
    PyObject *existing = PyObject_GetAttr(callback, attrName);
    if (existing && PyList_Check(existing)) {
        if (PyList_Append(existing, data->propertyName) < 0) {
            Py_DECREF(existing);
            Py_DECREF(callback);
            return nullptr;
        }
        Py_DECREF(existing);
    } else {
        Py_XDECREF(existing); // release any non-list object returned by GetAttr
        PyErr_Clear();
        AutoDecRef newList(PyList_New(1));
        if (newList.isNull()) {
            Py_DECREF(callback);
            return nullptr;
        }
        Py_INCREF(data->propertyName);
        PyList_SetItem(newList.object(), 0, data->propertyName);
        if (PyObject_SetAttr(callback, attrName, newList) < 0) {
            Py_DECREF(callback);
            return nullptr;
        }
    }

    return callback;
}

static void watchTpDealloc(PyObject *self)
{
    auto *data = reinterpret_cast<PySideWatch *>(self);
    Py_XDECREF(data->propertyName);
    Py_DECREF(Py_TYPE(self));
    PepExt_TypeCallFree(self);
}

static PyTypeObject *createWatchType()
{
    PyType_Slot PySideWatchType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(watchCall)},
        {Py_tp_init, reinterpret_cast<void *>(watchTpInit)},
        {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(watchTpDealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideWatchType_spec = {
        "2:PySide6.QtQmlFeatures.watch",
        sizeof(PySideWatch),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideWatchType_slots,
    };

    return SbkType_FromSpec(&PySideWatchType_spec);
}

static PyTypeObject *PySideWatch_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createWatchType);
#else
    static auto *type = createWatchType();
#endif
    return type;
}

} // extern "C"

namespace PySide::Watch {

PyObject *watchAttrName()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString s("_pyside_watch");
#else
    static PyObject *const s = Shiboken::String::createStaticString("_pyside_watch");
#endif
    return s;
}

static const char *Watch_SignatureStrings[] = {
    "PySide6.QtQmlFeatures.watch(self,property_name:str)",
    "PySide6.QtQmlFeatures.watch.__call__(self,function:typing.Callable)->typing.Callable",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *watchType = PySideWatch_TypeF();
    if (InitSignatureStrings(watchType, Watch_SignatureStrings) < 0)
        return;
    auto *obWatchType = reinterpret_cast<PyObject *>(watchType);
    Py_INCREF(obWatchType);
    PepModule_AddType(module, watchType);
}

} // namespace PySide::Watch
