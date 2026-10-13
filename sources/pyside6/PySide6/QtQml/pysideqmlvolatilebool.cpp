// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmlvolatilebool.h"

#include <basewrapper.h>
#include <pep384ext.h>
#include <signature.h>
#include <sbktypefactory.h>

#include <QtCore/QDebug>

// Volatile Bool used for QQmlIncubationController::incubateWhile(std::atomic<bool> *, int)

// Generated headers containing the definition of struct
// QtQml_VolatileBoolObject. It is injected to avoid "pyside6_qtqml_python.h"
// depending on other headers.
#include "pyside6_qtcore_python.h"
#include "pyside6_qtqml_python.h"

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

// VolatileBool (volatile bool) type definition.

static PyObject *
QtQml_VolatileBoolObject_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static const char *kwlist[] = {"x", 0};
    PyObject *x = Py_False;
    long ok;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:bool", const_cast<char **>(kwlist), &x))
        return nullptr;
    ok = PyObject_IsTrue(x);
    if (ok < 0)
        return nullptr;

    auto *self = PepExt_TypeCallAlloc<QtQml_VolatileBoolObject>(type, 0);

    if (self != nullptr)
        self->flag = new AtomicBool(ok);

    return reinterpret_cast<PyObject *>(self);
}

static void QtQml_VolatileBoolObject_dealloc(PyObject *self)
{
    auto volatileBool = reinterpret_cast<QtQml_VolatileBoolObject *>(self);
    delete volatileBool->flag;
    Sbk_object_dealloc(self);
}

static PyObject *
QtQml_VolatileBoolObject_get(QtQml_VolatileBoolObject *self)
{
    if (*self->flag) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject *
QtQml_VolatileBoolObject_set(QtQml_VolatileBoolObject *self, PyObject *args)
{
    PyObject *value = Py_False;
    long ok;

    if (!PyArg_ParseTuple(args, "O:bool", &value)) {
        return nullptr;
    }

    ok = PyObject_IsTrue(value);
    if (ok < 0)
        return PyErr_Format(PyExc_TypeError, "Not a boolean value.");

    *self->flag = ok > 0;

    Py_RETURN_NONE;
}

QT_WARNING_PUSH
QT_WARNING_DISABLE_CLANG("-Wcast-function-type-mismatch")
static PyMethodDef QtQml_VolatileBoolObject_methods[] = {
    {"get", reinterpret_cast<PyCFunction>(QtQml_VolatileBoolObject_get), METH_NOARGS,
     "B.get() -> Bool. Returns the value of the volatile boolean"
    },
    {"set", reinterpret_cast<PyCFunction>(QtQml_VolatileBoolObject_set), METH_VARARGS,
     "B.set(a) -> None. Sets the value of the volatile boolean"
    },
    {nullptr, nullptr, 0, nullptr}  /* Sentinel */
};
QT_WARNING_POP

static PyObject *
QtQml_VolatileBoolObject_repr(QtQml_VolatileBoolObject *self)
{
    const char *typeName = Py_TYPE(reinterpret_cast<PyObject *>(self))->tp_name;
    PyObject *s = *self->flag
        ? PyBytes_FromFormat("%s(True)", typeName)
        : PyBytes_FromFormat("%s(False)", typeName);
    Py_XINCREF(s);
    return s;
}

static PyObject *
QtQml_VolatileBoolObject_str(QtQml_VolatileBoolObject *self)
{
    const char *typeName = Py_TYPE(reinterpret_cast<PyObject *>(self))->tp_name;
    PyObject *s = *self->flag
        ? PyBytes_FromFormat("%s(True) -> %p", typeName, self->flag)
        : PyBytes_FromFormat("%s(False) -> %p", typeName, self->flag);
    Py_XINCREF(s);
    return s;
}

static PyTypeObject *createVolatileBoolType()
{
    PyType_Slot QtQml_VolatileBoolType_slots[] = {
        {Py_tp_repr, reinterpret_cast<void *>(QtQml_VolatileBoolObject_repr)},
        {Py_tp_str, reinterpret_cast<void *>(QtQml_VolatileBoolObject_str)},
        {Py_tp_methods, reinterpret_cast<void *>(QtQml_VolatileBoolObject_methods)},
        {Py_tp_new, reinterpret_cast<void *>(QtQml_VolatileBoolObject_new)},
        {Py_tp_dealloc, reinterpret_cast<void *>(QtQml_VolatileBoolObject_dealloc)},
        {0, 0}
    };

    PyType_Spec QtQml_VolatileBoolType_spec = {
        "2:PySide6.QtQml.VolatileBool",
        sizeof(QtQml_VolatileBoolObject),
        0,
        Py_TPFLAGS_DEFAULT,
        QtQml_VolatileBoolType_slots,
    };

    return SbkType_FromSpec(&QtQml_VolatileBoolType_spec);
}

PyTypeObject *QtQml_VolatileBool_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createVolatileBoolType);
#else
    static auto *type = createVolatileBoolType();
#endif
    return type;
}

static const char *VolatileBool_SignatureStrings[] = {
    "PySide6.QtQml.VolatileBool.get(self)->bool",
    "PySide6.QtQml.VolatileBool.set(self,a:object)",
    nullptr}; // Sentinel

void initQtQmlVolatileBool(PyObject *module)
{
    auto *qmlVolatileBoolType = QtQml_VolatileBool_TypeF();
    if (InitSignatureStrings(qmlVolatileBoolType, VolatileBool_SignatureStrings) < 0) {
        PyErr_Print();
        qWarning() << "Error initializing VolatileBool type.";
        return;
    }

    auto *obQmlVolatileBoolType = reinterpret_cast<PyObject *>(qmlVolatileBoolType);
    Py_INCREF(obQmlVolatileBoolType);
    PepModule_AddType(module, qmlVolatileBoolType);
}
