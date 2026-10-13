// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidesignal_p.h"
#include "pysideslot_p.h"
#include "pysidestaticstrings.h"
#include "pysideglobals_p.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <sbkpep.h>
#include <sbkpytuplerange.h>
#include <sbkstaticstrings.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtCore/qmetaobject.h>
#include <QtCore/qstring.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Shiboken;

struct SlotData
{
    QByteArray name;
    QByteArray args;
    QByteArray resultType;
    QByteArray tag; // QMetaMethod::tag()
};

struct PySideSlot
{
    PyObject_HEAD
    SlotData *slotData;
};

extern "C"
{

static void slotDataListDestructor(PyObject *o)
{
    delete PySide::Slot::dataListFromCapsule(o);
}

static int slotTpInit(PyObject *, PyObject *, PyObject *);
static PyObject *slotCall(PyObject *, PyObject *, PyObject *);

// Class Definition -----------------------------------------------

static PyTypeObject *createSlotType()
{
    PyType_Slot PySideSlotType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(slotCall)},
        {Py_tp_init, reinterpret_cast<void *>(slotTpInit)},
        {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideSlotType_spec = {
        "2:PySide6.QtCore.Slot",
        sizeof(PySideSlot),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideSlotType_slots,
    };

    return SbkType_FromSpec(&PySideSlotType_spec);
}

static PyTypeObject *PySideSlot_TypeF()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createSlotType);
#else
    static auto *type = createSlotType();
#endif
    return type;
}

int slotTpInit(PyObject *self, PyObject *args, PyObject *kw)
{
    static const char *kwlist[] = {"name", "result", "tag", nullptr};
    char *argName = nullptr;
    PyObject *argResult = nullptr;
    char *tag = nullptr;

#ifdef Py_GIL_DISABLED
    // Slot() constructors in several threads share one tuple
    static PyObject *const emptyTuple = PyTuple_New(0);
#else
    PyObject *& emptyTuple = PySide::globals()->emptyTuple;
    if (emptyTuple == nullptr)
        emptyTuple = PyTuple_New(0);
#endif

    if (!PyArg_ParseTupleAndKeywords(emptyTuple, kw, "|sOs:QtCore.Slot",
                                     const_cast<char **>(kwlist),
                                     &argName, &argResult, &tag)) {
        return -1;
    }

    auto *data = reinterpret_cast<PySideSlot *>(self);
    if (!data->slotData)
        data->slotData = new SlotData;
    for (auto *argType : Shiboken::PyTupleRange(args)) {
        const auto typeName = PySide::Signal::getTypeName(argType);
        if (typeName.isEmpty()) {
            PyErr_Format(PyExc_TypeError, "Unknown signal argument type: %s", Py_TYPE(argType)->tp_name);
            return -1;
        }
        if (!data->slotData->args.isEmpty())
            data->slotData->args += ',';
        data->slotData->args += typeName;
    }

    if (argName)
        data->slotData->name = argName;

    if (tag)
        data->slotData->tag = tag;

    data->slotData->resultType = argResult
        ? PySide::Signal::getTypeName(argResult) : PySide::Signal::voidType();

    return 0;
}

PyObject *slotCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *callback = nullptr;

    if (!PyArg_UnpackTuple(args, "Slot.__call__", 1, 1, &callback))
        return nullptr;
    Py_INCREF(callback);

    if (PyCallable_Check(callback)) {
        auto *data = reinterpret_cast<PySideSlot *>(self);

        if (!data->slotData)
            data->slotData = new SlotData;

        if (data->slotData->name.isEmpty()) {
            // PYSIDE-198: Use PyObject_GetAttr instead of PepFunction_GetName to support Nuitka.
            AutoDecRef funcName(PyObject_GetAttr(callback, PyMagicName::name()));
            data->slotData->name = funcName.isNull() ? "<no name>" : String::toCString(funcName);
        }
        const QByteArray returnType = QMetaObject::normalizedType(data->slotData->resultType);
        const QByteArray signature = data->slotData->name + '(' + data->slotData->args + ')';

        PyObject *pySlotName = PySide::PySideMagicName::slot_list_attr();
        PySide::Slot::DataList *entryList = nullptr;
        if (PyObject_HasAttr(callback, pySlotName)) {
            auto *capsule = PyObject_GetAttr(callback, pySlotName);
            entryList = PySide::Slot::dataListFromCapsule(capsule);
        }
        if (entryList == nullptr) { // PYSIDE-3307: unittest.mock/patch might interfere.
            entryList = new PySide::Slot::DataList{};
            auto *capsule = PyCapsule_New(entryList, nullptr /* name */, slotDataListDestructor);
            Py_INCREF(capsule);
            PyObject_SetAttr(callback, pySlotName, capsule);
        }
        entryList->append({signature, returnType, data->slotData->tag});

        //clear data
        delete data->slotData;
        data->slotData = nullptr;
    }
    return callback;
}

} // extern "C"

/*!
    \namespace PySide::Slot
    \brief Namespace for @Slot.
    \inmodule libpyside
*/
namespace PySide::Slot {

DataList *dataListFromCapsule(PyObject *capsule)
{
    if (capsule != nullptr && PyCapsule_CheckExact(capsule) != 0) {
        if (void *v = PyCapsule_GetPointer(capsule, nullptr))
            return reinterpret_cast<DataList *>(v);
    }
    return nullptr;
}

static const char *Slot_SignatureStrings[] = {
    "PySide6.QtCore.Slot(self,*types:typing.Union[type,str],name:str=nullptr,result:typing.Union[type,str]=nullptr)",
    "PySide6.QtCore.Slot.__call__(self,function:_SlotFunc)->_SlotFunc",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    auto *slotType = PySideSlot_TypeF();
    if (InitSignatureStrings(slotType, Slot_SignatureStrings) < 0)
        return;

    auto *obSlotType = reinterpret_cast<PyObject *>(slotType);
    Py_INCREF(obSlotType);
    PepModule_AddType(module, slotType);
}

} // namespace PySide::Slot
