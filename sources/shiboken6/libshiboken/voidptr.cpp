// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "voidptr.h"
#include "pep384ext.h"
#include "sbkconverter.h"
#include "basewrapper.h"
#include "basewrapper_p.h"
#include "sbktypefactory.h"

#ifdef Py_GIL_DISABLED
#  include "sbkheldlocks.h"
#endif

extern "C"
{

// Void pointer object definition.
struct SbkVoidPtrObject {
    PyObject_HEAD
    void *cptr;
    Py_ssize_t size;
    bool isWritable;
};

PyObject *SbkVoidPtrObject_new(PyTypeObject *type, PyObject * /* args */, PyObject * /* kwds */)
{
    // PYSIDE-560: It is much safer to first call a function and then do a
    // type cast than to do everything in one line. The bad construct looked
    // like this, actual call forgotten:
    //    SbkVoidPtrObject *self =
    //        reinterpret_cast<SbkVoidPtrObject *>(type->tp_alloc);
    auto *self = PepExt_TypeCallAlloc<SbkVoidPtrObject>(type, 0);

    if (self != nullptr) {
        self->cptr = nullptr;
        self->size = -1;
        self->isWritable = false;
    }

    return reinterpret_cast<PyObject *>(self);
}

static inline bool SbkVoidPtr_Check(PyObject *op)
{
    return Py_TYPE(op) == SbkVoidPtr_TypeF();
}

int SbkVoidPtrObject_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *addressObject{};
    Py_ssize_t size = -1;
    int isWritable = 0;
    auto *sbkSelf = reinterpret_cast<SbkVoidPtrObject *>(self);

    static const char *kwlist[] = {"address", "size", "writeable", nullptr};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|ni", const_cast<char **>(kwlist),
                                     &addressObject, &size, &isWritable))
        return -1;

    if (addressObject == Py_None) {
        sbkSelf->cptr = nullptr;
        sbkSelf->size = 0;
        sbkSelf->isWritable = false;
        return 0;
    }
    // Void pointer (copy).
    if (SbkVoidPtr_Check(addressObject)) {
        auto *sbkOther = reinterpret_cast<SbkVoidPtrObject *>(addressObject);
        sbkSelf->cptr = sbkOther->cptr;
        sbkSelf->size = sbkOther->size;
        sbkSelf->isWritable = sbkOther->isWritable;
        return 0;
    }
    // Python buffer interface.
    if (PyObject_CheckBuffer(addressObject)) {
        Py_buffer bufferView;

        // Bail out if the object can't provide a simple contiguous buffer.
        if (PyObject_GetBuffer(addressObject, &bufferView, PyBUF_SIMPLE) < 0)
            return -1;

        sbkSelf->cptr = bufferView.buf;
        sbkSelf->size = bufferView.len > 0 ? bufferView.len : size;
        sbkSelf->isWritable = bufferView.readonly <= 0;

        // Release the buffer.
        PyBuffer_Release(&bufferView);
        return 0;
    }
    // Shiboken::Object wrapper.
    if (Shiboken::Object::checkType(addressObject)) {
        auto *sbkOther = reinterpret_cast<SbkObject *>(addressObject);
        sbkSelf->cptr = Shiboken::Object::cppPointer(sbkOther);
        sbkSelf->size = size;
        sbkSelf->isWritable = isWritable > 0;
        return 0;
    }

    // An integer representing an address.
    void *cptr = PyLong_AsVoidPtr(addressObject);
    if (PyErr_Occurred() != nullptr) {
        PyErr_SetString(PyExc_TypeError,
                        "Creating a VoidPtr object requires an address of a C++ object, "
                        "a wrapped Shiboken Object type, "
                        "an object implementing the Python Buffer interface, "
                        "or another VoidPtr object.");
        return -1;
    }
    sbkSelf->cptr = cptr;
    sbkSelf->size = size;
    sbkSelf->isWritable = isWritable > 0;

    return 0;
}

PyObject *SbkVoidPtrObject_richcmp(PyObject *obj1, PyObject *obj2, int op)
{
    PyObject *result = Py_False;
    void *cptr1 = nullptr;
    void *cptr2 = nullptr;
    bool validObjects = true;

    if (SbkVoidPtr_Check(obj1))
        cptr1 = reinterpret_cast<SbkVoidPtrObject *>(obj1)->cptr;
    else
        validObjects = false;

    if (SbkVoidPtr_Check(obj2))
        cptr2 = reinterpret_cast<SbkVoidPtrObject *>(obj2)->cptr;
    else
        validObjects = false;

    if (validObjects) {
        switch (op) {
        case Py_EQ:
            if (cptr1 == cptr2)
                result = Py_True;
            break;
        case Py_NE:
            if (cptr1 != cptr2)
                result = Py_True;
            break;
        default:
            break;
        }
    }

    Py_INCREF(result);
    return result;
}

PyObject *SbkVoidPtrObject_int(PyObject *v)
{
    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(v);
    return PyLong_FromVoidPtr(sbkObject->cptr);
}

PyObject *toBytes(PyObject *self, PyObject * /* args */)
{
    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(self);
    if (sbkObject->size < 0)
        return PyErr_Format(PyExc_IndexError, "libshiboken: VoidPtr does not have a size set.");

    PyObject *bytes = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(sbkObject->cptr),
                                                sbkObject->size);
    Py_XINCREF(bytes);
    return bytes;
}

#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
static struct PyMethodDef SbkVoidPtrObject_methods[] = {
    {"toBytes", toBytes, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}
};
#ifdef __clang__
#  pragma clang diagnostic pop
#endif

static Py_ssize_t SbkVoidPtrObject_length(PyObject *v)
{
    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(v);
    if (sbkObject->size < 0) {
        PyErr_SetString(PyExc_IndexError, "libshiboken: VoidPtr does not have a size set.");
        return -1;
    }

    return sbkObject->size;
}

static const char trueString[] = "True" ;
static const char falseString[] = "False" ;

PyObject *SbkVoidPtrObject_repr(PyObject *v)
{
    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(v);
    PyObject *s = PyUnicode_FromFormat("%s(%p, %zd, %s)",
                           Py_TYPE(v)->tp_name,
                           sbkObject->cptr,
                           sbkObject->size,
                           sbkObject->isWritable ? trueString : falseString);
    Py_XINCREF(s);
    return s;
}

PyObject *SbkVoidPtrObject_str(PyObject *v)
{
    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(v);
    PyObject *s = PyUnicode_FromFormat("%s(Address %p, Size %zd, isWritable %s)",
                           Py_TYPE(v)->tp_name,
                           sbkObject->cptr,
                           sbkObject->size,
                           sbkObject->isWritable ? trueString : falseString);
    Py_XINCREF(s);
    return s;
}


static int SbkVoidPtrObject_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
    if (view == nullptr)
        return -1;

    auto *sbkObject = reinterpret_cast<SbkVoidPtrObject *>(obj);
    if (sbkObject->size < 0)
        return -1;

    int readonly = sbkObject->isWritable ? 0 : 1;
    if (((flags & PyBUF_WRITABLE) == PyBUF_WRITABLE) &&
        (readonly == 1)) {
        PyErr_SetString(PyExc_BufferError,
                        "Object is not writable.");
        return -1;
    }

    view->obj = obj;
    if (obj)
        Py_XINCREF(obj);
    view->buf = sbkObject->cptr;
    view->len = sbkObject->size;
    view->readonly = readonly;
    view->itemsize = 1;
    view->format = nullptr;
    if ((flags & PyBUF_FORMAT) == PyBUF_FORMAT)
        view->format = const_cast<char *>("B");
    view->ndim = 1;
    view->shape = nullptr;
    if ((flags & PyBUF_ND) == PyBUF_ND)
        view->shape = &(view->len);
    view->strides = nullptr;
    if ((flags & PyBUF_STRIDES) == PyBUF_STRIDES)
        view->strides = &(view->itemsize);
    view->suboffsets = nullptr;
    view->internal = nullptr;
    return 0;
}

static PyBufferProcs SbkVoidPtrObjectBufferProc = {
    SbkVoidPtrObject_getbuffer, // bf_getbuffer
    nullptr                     // bf_releasebuffer
};

static PyTypeObject *createVoidPtrType()
{
    PyType_Slot SbkVoidPtrType_slots[] = {
        {Py_tp_repr, reinterpret_cast<void *>(SbkVoidPtrObject_repr)},
        {Py_nb_int, reinterpret_cast<void *>(SbkVoidPtrObject_int)},
        {Py_sq_length, reinterpret_cast<void *>(SbkVoidPtrObject_length)},
        {Py_tp_str, reinterpret_cast<void *>(SbkVoidPtrObject_str)},
        {Py_tp_richcompare, reinterpret_cast<void *>(SbkVoidPtrObject_richcmp)},
        {Py_tp_init, reinterpret_cast<void *>(SbkVoidPtrObject_init)},
        {Py_tp_new, reinterpret_cast<void *>(SbkVoidPtrObject_new)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Sbk_object_dealloc)},
        {Py_tp_methods, reinterpret_cast<void *>(SbkVoidPtrObject_methods)},
        {0, nullptr}
    };

    PyType_Spec SbkVoidPtrType_spec = {
        "2:shiboken6.Shiboken.VoidPtr",
        sizeof(SbkVoidPtrObject),
        0,
        Py_TPFLAGS_DEFAULT,
        SbkVoidPtrType_slots,
    };

    return SbkType_FromSpec_BMDWB(&SbkVoidPtrType_spec,
                                  nullptr, nullptr, 0, 0,
                                  &SbkVoidPtrObjectBufferProc);
}

PyTypeObject *SbkVoidPtr_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createVoidPtrType);
#else
    static auto *type = createVoidPtrType();
#endif
    return type;
}

} // extern "C"

/*!
    \namespace VoidPtr

    \brief Namespace for VoidPtr-related functionality.

    \inmodule libshiboken
*/
namespace VoidPtr {

static int voidPointerInitialized = false;

void init()
{
    if (PyType_Ready(SbkVoidPtr_TypeF()) < 0)
        Py_FatalError("libshiboken: Failed to initialize Shiboken.VoidPtr type.");
    else
        voidPointerInitialized = true;
}

void addVoidPtrToModule(PyObject *module)
{
    if (voidPointerInitialized) {
        auto *type = SbkVoidPtr_TypeF();
        auto *obType = reinterpret_cast<PyObject *>(type);
        Py_INCREF(obType);
        PepModule_AddType(module, type);
    }
}

static PyObject *createVoidPtr(void *cppIn, Py_ssize_t size = 0, bool isWritable = false)
{
    if (!cppIn)
        Py_RETURN_NONE;

    SbkVoidPtrObject *result = PyObject_New(SbkVoidPtrObject, SbkVoidPtr_TypeF());
    if (!result)
        Py_RETURN_NONE;

    result->cptr = cppIn;
    result->size = size;
    result->isWritable = isWritable;

    return reinterpret_cast<PyObject *>(result);
}

static PyObject *toPython(const void *cppIn)
{
    return createVoidPtr(const_cast<void *>(cppIn));
}

static void VoidPtrToCpp(PyObject *pyIn, void *cppOut)
{
    auto *sbkIn = reinterpret_cast<SbkVoidPtrObject *>(pyIn);
    *reinterpret_cast<void **>(cppOut) = sbkIn->cptr;
}

static PythonToCppFunc VoidPtrToCppIsConvertible(PyObject *pyIn)
{
    return SbkVoidPtr_Check(pyIn) ? VoidPtrToCpp : nullptr;
}

static void SbkObjectToCpp(PyObject *pyIn, void *cppOut)
{
    auto *sbkIn = reinterpret_cast<SbkObject *>(pyIn);
    *reinterpret_cast<void **>(cppOut) = Shiboken::Object::cppPointer(sbkIn);
}

static PythonToCppFunc SbkObjectToCppIsConvertible(PyObject *pyIn)
{
    return Shiboken::Object::checkType(pyIn) ? SbkObjectToCpp : nullptr;
}

static void PythonBufferToCpp(PyObject *pyIn, void *cppOut)
{
    if (PyObject_CheckBuffer(pyIn)) {
        Py_buffer bufferView;

        // Bail out if the object can't provide a simple contiguous buffer.
        if (PyObject_GetBuffer(pyIn, &bufferView, PyBUF_SIMPLE) < 0)
            return;

        *reinterpret_cast<void **>(cppOut) = bufferView.buf;

        // Release the buffer.
        PyBuffer_Release(&bufferView);
    }
}

static PythonToCppFunc PythonBufferToCppIsConvertible(PyObject *pyIn)
{
    if (PyObject_CheckBuffer(pyIn)) {
        Py_buffer bufferView;

        // Bail out if the object can't provide a simple contiguous buffer.
        if (PyObject_GetBuffer(pyIn, &bufferView, PyBUF_SIMPLE) < 0)
            return nullptr;

        // Release the buffer.
        PyBuffer_Release(&bufferView);

        return PythonBufferToCpp;
    }
    return nullptr;
}

SbkConverter *createConverter()
{
    SbkConverter *converter = Shiboken::Conversions::createConverter(SbkVoidPtr_TypeF(), toPython);
    Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                         VoidPtrToCpp,
                                                         VoidPtrToCppIsConvertible);
    Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                         SbkObjectToCpp,
                                                         SbkObjectToCppIsConvertible);
    Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                         PythonBufferToCpp,
                                                         PythonBufferToCppIsConvertible);
    return converter;
}

void setSize(PyObject *voidPtr, Py_ssize_t size)
{
    assert(voidPtr->ob_type == SbkVoidPtr_TypeF());
    auto *voidPtrObj = reinterpret_cast<SbkVoidPtrObject *>(voidPtr);
    voidPtrObj->size = size;
}

Py_ssize_t getSize(PyObject *voidPtr)
{
    assert(voidPtr->ob_type == SbkVoidPtr_TypeF());
    auto *voidPtrObj = reinterpret_cast<SbkVoidPtrObject *>(voidPtr);
    return voidPtrObj->size;
}

bool isWritable(PyObject *voidPtr)
{
    assert(voidPtr->ob_type == SbkVoidPtr_TypeF());
    auto *voidPtrObj = reinterpret_cast<SbkVoidPtrObject *>(voidPtr);
    return voidPtrObj->isWritable;
}

void setWritable(PyObject *voidPtr, bool isWritable)
{
    assert(voidPtr->ob_type == SbkVoidPtr_TypeF());
    auto *voidPtrObj = reinterpret_cast<SbkVoidPtrObject *>(voidPtr);
    voidPtrObj->isWritable = isWritable;
}

} // namespace VoidPtr
