// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbktypefactory.h"
#include "autodecref.h"
#include "sbkpep.h"
#include "sbkpepbuffer.h"
#include "sbkpytuplerange.h"
#include "sbkstring.h"
#include "sbkstaticstrings.h"
#ifdef Py_GIL_DISABLED
#  include "basewrapper.h"
#  include "basewrapper_p.h"
#  include "sbkftoptions.h"
#endif

#include <cstring>
#include <iostream>
#include <utility>

extern "C"
{

using Shiboken::AutoDecRef;

PyTypeObject *SbkType_FromSpec(PyType_Spec *spec)
{
    return SbkType_FromSpec_BMDWB(spec, nullptr, nullptr, 0, 0, nullptr);
}

PyTypeObject *SbkType_FromSpecWithMeta(PyType_Spec *spec, PyTypeObject *meta)
{
    return SbkType_FromSpec_BMDWB(spec, nullptr, meta, 0, 0, nullptr);
}

PyTypeObject *SbkType_FromSpecWithBases(PyType_Spec *spec, PyObject *bases)
{
    return SbkType_FromSpec_BMDWB(spec, bases, nullptr, 0, 0, nullptr);
}

PyTypeObject *SbkType_FromSpecBasesMeta(PyType_Spec *spec, PyObject *bases, PyTypeObject *meta)
{
    return SbkType_FromSpec_BMDWB(spec, bases, meta, 0, 0, nullptr);
}

#ifdef PYPY_VERSION

static PyObject *_PyType_FromSpecWithBases(PyType_Spec *, PyObject *);

#else

#define _PyType_FromSpecWithBases PyType_FromSpecWithBases

#endif // PYPY_VERSION

// PYSIDE-2230: Not so temporary fix for Python 3.12.
//              A tp_new is no longer allowed in a meta class.
//              Hopefully, the Python devs will supply the missing support.
//              It turned out that they will not fix that, as expected.
// Note: Python 3.12 is the first version that grabs the metaclass from base classes.
#ifdef Py_GIL_DISABLED
// Free-threaded builds change no shared type object: another thread could
// create its type in between. PyType_FromMetaclass refuses the tp_new of
// Shiboken.ObjectType, so its types are made by a subclass of it with the
// tp_new of type, and then get their metaclass. `object` is not patched,
// PyType_FromMetaclass takes the metaclass directly.
static PyObject *_PyType_FromSpecWithBasesHack(PyType_Spec *spec,
                                               PyObject *bases,
                                               PyTypeObject *meta)
{
    AutoDecRef basesPatch{};
    if (bases == Py_None) {
        // PYSIDE-2230: The SbkObject entry, which has no base to provide the metaclass.
        assert(meta);
        basesPatch.reset(Py_BuildValue("(O)", &PyBaseObject_Type));
        bases = basesPatch.object();
    }

    // The metaclass with a tp_new of its own, as the patch below finds it
    PyTypeObject *customMeta = meta != nullptr && meta->tp_new != PyType_Type.tp_new
        ? meta : nullptr;
    if (bases != nullptr) {
        for (auto *obBase : Shiboken::PyTupleRange(bases)) {
            if (Py_TYPE(obBase)->tp_new != PyType_Type.tp_new)
                customMeta = Py_TYPE(obBase);
        }
    }
    // Only Shiboken.ObjectType has one; any other gets CPython's TypeError
    PyTypeObject *factoryMeta = meta;
    if (customMeta != nullptr && SbkObjectType_FactoryTypeF()->tp_base == customMeta)
        factoryMeta = SbkObjectType_FactoryTypeF();
    auto *ret = PyType_FromMetaclass(factoryMeta, nullptr /*module*/, spec, bases);
    if (ret != nullptr && factoryMeta != meta) {
        PyTypeObject *hold = std::exchange(ret->ob_type, customMeta);
        Py_INCREF(reinterpret_cast<PyObject *>(customMeta));
        Py_DECREF(reinterpret_cast<PyObject *>(hold));
    }
    return ret;
}

// The __qualname__ setter stops the world on Python 3.15, once per type, so
// threads that make types meet in a stop-the-world storm. A type that
// PyType_FromMetaclass() has just made still has ht_qualname == ht_name: the
// new value is stored directly, and the old one stays alive through ht_name
// for a thread that reads it meanwhile.
static int setNewTypeQualname(PyTypeObject *type, PyObject *qualname)
{
    auto *ht = reinterpret_cast<PyHeapTypeObject *>(type);
    if (!Shiboken::FreeThreading::optionEnabled(Shiboken::FreeThreading::Option::NewTypeQualname)
        || ht->ht_qualname != ht->ht_name) {
        return PyObject_SetAttr(reinterpret_cast<PyObject *>(type),
                                Shiboken::PyMagicName::qualname(), qualname);
    }
    PyObject *old = ht->ht_qualname;
    _Py_atomic_store_ptr_release(&ht->ht_qualname, Py_NewRef(qualname));
    Py_DECREF(old);
    return 0;
}
#else // Py_GIL_DISABLED
static PyObject *_PyType_FromSpecWithBasesHack(PyType_Spec *spec,
                                               PyObject *bases,
                                               PyTypeObject *meta)
{
    PyTypeObject *keepMeta{};
    newfunc keepNew{};
    AutoDecRef basesPatch{};

    if (bases) {
        if (bases == Py_None) {
            // PYSIDE-2230: This is the SbkObject entry which has no base to provide
            //              the metaclass. We patch it in by modifying `object`s class.
            assert(meta);
            auto *base = reinterpret_cast<PyObject *>(&PyBaseObject_Type);
            base->ob_type = meta;
            basesPatch.reset(Py_BuildValue("(O)", &PyBaseObject_Type));
            bases = basesPatch.object();
        }

        for (auto [idx, obBase] : Shiboken::PyTupleEnumeratedRange(bases)) {
            auto *base = reinterpret_cast<PyTypeObject *>(obBase);
            PyTypeObject *meta = Py_TYPE(obBase);
            if (meta->tp_new != PyType_Type.tp_new) {
                // make sure there is no second meta class
                if (keepMeta != nullptr) {
                    std::cerr << "Warning: " << __FUNCTION__
                        << ": multiple meta classes found for " << spec->name << " at "
                        << idx << ": " << PepType_GetFullyQualifiedNameStr(base)
                        << " in addition to "
                        << PepType_GetFullyQualifiedNameStr(keepMeta) << '\n';
                }
                assert(keepMeta == nullptr);
                keepMeta = meta;
                keepNew = meta->tp_new;
                meta->tp_new = PyType_Type.tp_new;
            }
        }
    }

#if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030C0000
    auto *ret = PyType_FromMetaclass(meta, nullptr /*module*/, spec, bases);
#else
    auto *ret = _PyType_FromSpecWithBases(spec, bases);
#endif

    if (keepMeta)
        keepMeta->tp_new = keepNew;
    if (basesPatch.object()) {
        // undo the metaclass patch.
        auto *base = PyTuple_GetItem(basesPatch.object(), 0);
        base->ob_type = &PyType_Type;
    }
    return ret;
}
#endif // Py_GIL_DISABLED

/*!
    \fn PyTypeObject *SbkType_FromSpec_BMDWB(PyType_Spec *spec, PyObject *bases, PyTypeObject *meta, int dictoffset, int weaklistoffset, PyBufferProcs *bufferprocs)

    Creates a type from \a spec with \a bases, the metaclass \a meta, and
    \a dictoffset, \a weaklistoffset and \a bufferprocs. On free-threaded
    builds, no shared type object is changed.
*/
PyTypeObject *SbkType_FromSpec_BMDWB(PyType_Spec *spec,
                                     PyObject *bases,
                                     PyTypeObject *meta,
                                     int dictoffset,
                                     int weaklistoffset,
                                     PyBufferProcs *bufferprocs)
{
    // PYSIDE-1286: Generate correct __module__ and __qualname__
    // The name field can now be extended by an "n:" prefix which is
    // the number of modules in the name. The default is 1.
    //
    // Example:
    //    "2:mainmod.submod.mainclass.subclass"
    // results in
    //    __module__   : "mainmod.submod"
    //    __qualname__ : "mainclass.subclass"
    //    __name__     : "subclass"

    PyType_Spec new_spec = *spec;
    const char *colon = std::strchr(spec->name, ':');
    assert(colon);
    int package_level = atoi(spec->name);
    const char *mod = new_spec.name = colon + 1;

    PyObject *obType = _PyType_FromSpecWithBasesHack(&new_spec, bases, meta);
    if (obType == nullptr)
        return nullptr;

    const char *qual = mod;
    for (int idx = package_level; idx > 0; --idx) {
        const char *dot = std::strchr(qual, '.');
        if (!dot)
            break;
        qual = dot + 1;
    }
    int mlen = qual - mod - 1;
    AutoDecRef module(Shiboken::String::fromCString(mod, mlen));
    AutoDecRef qualname(Shiboken::String::fromCString(qual));

    auto *type = reinterpret_cast<PyTypeObject *>(obType);

    if (meta) {
        PyTypeObject *hold = std::exchange(obType->ob_type, meta);
        Py_INCREF(reinterpret_cast<PyObject *>(Py_TYPE(obType)));
        if (hold->tp_flags & Py_TPFLAGS_HEAPTYPE)
            Py_DECREF(reinterpret_cast<PyObject *>(hold));
    }

    if (dictoffset)
        type->tp_dictoffset = dictoffset;
    if (weaklistoffset)
        type->tp_weaklistoffset = weaklistoffset;
    if (bufferprocs)
        PepType_AS_BUFFER(type) = bufferprocs;

#ifdef PYPY_VERSION
    // PYSIDE-535: Careful: Using PyObject_SetAttr would have the side-effect of calling
    // PyType_Ready too early. (at least in PyPy, which caused pretty long debugging.)
    auto *ht = reinterpret_cast<PyHeapTypeObject *>(type);
    ht->ht_qualname = qualname;
    AutoDecRef tpDict(PepType_GetDict(type));
    if (PyDict_SetItem(tpDict.object(), Shiboken::PyMagicName::qualname(), qualname))
        return nullptr;
    if (PyDict_SetItem(tpDict.object(), Shiboken::PyMagicName::module(), module))
        return nullptr;
    PyType_Ready(type);
#else
    if (PyObject_SetAttr(obType, Shiboken::PyMagicName::module(), module) < 0)
        return nullptr;
#  ifdef Py_GIL_DISABLED
    if (setNewTypeQualname(type, qualname) < 0)
        return nullptr;
#  else
    if (PyObject_SetAttr(obType, Shiboken::PyMagicName::qualname(), qualname) < 0)
        return nullptr;
#  endif
    PyType_Modified(type);
#endif
    return type;
}

#ifdef PYPY_VERSION

/////////////////////////////////////////////////////////////////////////////
//
// Reimplementation of `PyType_FromSpecWithBases`
//
// This is almost the original code from Python 3.7 with a few changes.
// Especially the call to `PyType_Ready` is deferred until the needed
// post-actions are carried out in `SbkType_FromSpec_BMDWBD`.
//
// FIXME remove ASAP.
// Version is not clear, yet. Current version == 7.3.6
//

static const short slotoffsets[] = {
    -1, /* invalid slot */
/* Generated by typeslots.py */
0,
0,
offsetof(PyHeapTypeObject, as_mapping.mp_ass_subscript),
offsetof(PyHeapTypeObject, as_mapping.mp_length),
offsetof(PyHeapTypeObject, as_mapping.mp_subscript),
offsetof(PyHeapTypeObject, as_number.nb_absolute),
offsetof(PyHeapTypeObject, as_number.nb_add),
offsetof(PyHeapTypeObject, as_number.nb_and),
offsetof(PyHeapTypeObject, as_number.nb_bool),
offsetof(PyHeapTypeObject, as_number.nb_divmod),
offsetof(PyHeapTypeObject, as_number.nb_float),
offsetof(PyHeapTypeObject, as_number.nb_floor_divide),
offsetof(PyHeapTypeObject, as_number.nb_index),
offsetof(PyHeapTypeObject, as_number.nb_inplace_add),
offsetof(PyHeapTypeObject, as_number.nb_inplace_and),
offsetof(PyHeapTypeObject, as_number.nb_inplace_floor_divide),
offsetof(PyHeapTypeObject, as_number.nb_inplace_lshift),
offsetof(PyHeapTypeObject, as_number.nb_inplace_multiply),
offsetof(PyHeapTypeObject, as_number.nb_inplace_or),
offsetof(PyHeapTypeObject, as_number.nb_inplace_power),
offsetof(PyHeapTypeObject, as_number.nb_inplace_remainder),
offsetof(PyHeapTypeObject, as_number.nb_inplace_rshift),
offsetof(PyHeapTypeObject, as_number.nb_inplace_subtract),
offsetof(PyHeapTypeObject, as_number.nb_inplace_true_divide),
offsetof(PyHeapTypeObject, as_number.nb_inplace_xor),
offsetof(PyHeapTypeObject, as_number.nb_int),
offsetof(PyHeapTypeObject, as_number.nb_invert),
offsetof(PyHeapTypeObject, as_number.nb_lshift),
offsetof(PyHeapTypeObject, as_number.nb_multiply),
offsetof(PyHeapTypeObject, as_number.nb_negative),
offsetof(PyHeapTypeObject, as_number.nb_or),
offsetof(PyHeapTypeObject, as_number.nb_positive),
offsetof(PyHeapTypeObject, as_number.nb_power),
offsetof(PyHeapTypeObject, as_number.nb_remainder),
offsetof(PyHeapTypeObject, as_number.nb_rshift),
offsetof(PyHeapTypeObject, as_number.nb_subtract),
offsetof(PyHeapTypeObject, as_number.nb_true_divide),
offsetof(PyHeapTypeObject, as_number.nb_xor),
offsetof(PyHeapTypeObject, as_sequence.sq_ass_item),
offsetof(PyHeapTypeObject, as_sequence.sq_concat),
offsetof(PyHeapTypeObject, as_sequence.sq_contains),
offsetof(PyHeapTypeObject, as_sequence.sq_inplace_concat),
offsetof(PyHeapTypeObject, as_sequence.sq_inplace_repeat),
offsetof(PyHeapTypeObject, as_sequence.sq_item),
offsetof(PyHeapTypeObject, as_sequence.sq_length),
offsetof(PyHeapTypeObject, as_sequence.sq_repeat),
offsetof(PyHeapTypeObject, ht_type.tp_alloc),
offsetof(PyHeapTypeObject, ht_type.tp_base),
offsetof(PyHeapTypeObject, ht_type.tp_bases),
offsetof(PyHeapTypeObject, ht_type.tp_call),
offsetof(PyHeapTypeObject, ht_type.tp_clear),
offsetof(PyHeapTypeObject, ht_type.tp_dealloc),
offsetof(PyHeapTypeObject, ht_type.tp_del),
offsetof(PyHeapTypeObject, ht_type.tp_descr_get),
offsetof(PyHeapTypeObject, ht_type.tp_descr_set),
offsetof(PyHeapTypeObject, ht_type.tp_doc),
offsetof(PyHeapTypeObject, ht_type.tp_getattr),
offsetof(PyHeapTypeObject, ht_type.tp_getattro),
offsetof(PyHeapTypeObject, ht_type.tp_hash),
offsetof(PyHeapTypeObject, ht_type.tp_init),
offsetof(PyHeapTypeObject, ht_type.tp_is_gc),
offsetof(PyHeapTypeObject, ht_type.tp_iter),
offsetof(PyHeapTypeObject, ht_type.tp_iternext),
offsetof(PyHeapTypeObject, ht_type.tp_methods),
offsetof(PyHeapTypeObject, ht_type.tp_new),
offsetof(PyHeapTypeObject, ht_type.tp_repr),
offsetof(PyHeapTypeObject, ht_type.tp_richcompare),
offsetof(PyHeapTypeObject, ht_type.tp_setattr),
offsetof(PyHeapTypeObject, ht_type.tp_setattro),
offsetof(PyHeapTypeObject, ht_type.tp_str),
offsetof(PyHeapTypeObject, ht_type.tp_traverse),
offsetof(PyHeapTypeObject, ht_type.tp_members),
offsetof(PyHeapTypeObject, ht_type.tp_getset),
offsetof(PyHeapTypeObject, ht_type.tp_free),
offsetof(PyHeapTypeObject, as_number.nb_matrix_multiply),
offsetof(PyHeapTypeObject, as_number.nb_inplace_matrix_multiply),
offsetof(PyHeapTypeObject, as_async.am_await),
offsetof(PyHeapTypeObject, as_async.am_aiter),
offsetof(PyHeapTypeObject, as_async.am_anext),
offsetof(PyHeapTypeObject, ht_type.tp_finalize),
};

static PyTypeObject *
best_base(PyObject *bases)
{
    // We always have only one base
    return reinterpret_cast<PyTypeObject *>(PyTuple_GetItem(bases, 0));
}

static PyObject *
_PyType_FromSpecWithBases(PyType_Spec *spec, PyObject *bases)
{
    PyHeapTypeObject *res = reinterpret_cast<PyHeapTypeObject *>(
                                PyType_GenericAlloc(&PyType_Type, 0));
    PyTypeObject *type, *base;
    PyObject *modname;
    char *s;
    char *res_start = reinterpret_cast<char *>(res);
    PyType_Slot *slot;

    if (res == nullptr)
        return nullptr;

    if (spec->name == nullptr) {
        PyErr_SetString(PyExc_SystemError,
                        "Type spec does not define the name field.");
        goto fail;
    }

    /* Set the type name and qualname */
    s = std::strrchr(const_cast<char *>(spec->name), '.');
    if (s == nullptr)
        s = (char*)spec->name;
    else
        s++;

    type = &res->ht_type;
    /* The flags must be initialized early, before the GC traverses us */
    type->tp_flags = spec->flags | Py_TPFLAGS_HEAPTYPE;
    res->ht_name = PyUnicode_FromString(s);
    if (!res->ht_name)
        goto fail;
    res->ht_qualname = res->ht_name;
    Py_INCREF(res->ht_qualname);
    type->tp_name = spec->name;

    /* Adjust for empty tuple bases */
    if (!bases) {
        base = &PyBaseObject_Type;
        /* See whether Py_tp_base(s) was specified */
        for (slot = spec->slots; slot->slot; slot++) {
            if (slot->slot == Py_tp_base)
                base = reinterpret_cast<PyTypeObject *>(slot->pfunc);
            else if (slot->slot == Py_tp_bases) {
                bases = reinterpret_cast<PyObject *>(slot->pfunc);
                Py_INCREF(bases);
            }
        }
        if (!bases)
            bases = PyTuple_Pack(1, base);
        if (!bases)
            goto fail;
    }
    else
        Py_INCREF(bases);

    /* Calculate best base, and check that all bases are type objects */
    base = best_base(bases);
    if (base == nullptr) {
        goto fail;
    }

    /* Initialize essential fields */
    type->tp_as_async = &res->as_async;
    type->tp_as_number = &res->as_number;
    type->tp_as_sequence = &res->as_sequence;
    type->tp_as_mapping = &res->as_mapping;
    type->tp_as_buffer = &res->as_buffer;
    /* Set tp_base and tp_bases */
    type->tp_bases = bases;
    bases = nullptr;
    Py_INCREF(base);
    type->tp_base = base;

    type->tp_basicsize = spec->basicsize;
    type->tp_itemsize = spec->itemsize;

    for (slot = spec->slots; slot->slot; slot++) {
        if (slot->slot == Py_tp_base || slot->slot == Py_tp_bases)
            /* Processed above */
            continue;
        *reinterpret_cast<void **>(res_start + slotoffsets[slot->slot]) = slot->pfunc;

        /* need to make a copy of the docstring slot, which usually
           points to a static string literal */
        if (slot->slot == Py_tp_doc) {
            const char *old_doc = reinterpret_cast<char *>(slot->pfunc);
            //_PyType_DocWithoutSignature(type->tp_name, slot->pfunc);
            size_t len = std::strlen(old_doc)+1;
            char *tp_doc = reinterpret_cast<char *>(PyObject_MALLOC(len));
            if (tp_doc == nullptr) {
                type->tp_doc = nullptr;
                PyErr_NoMemory();
                goto fail;
            }
            memcpy(tp_doc, old_doc, len);
            type->tp_doc = tp_doc;
        }
    }
    if (type->tp_dealloc == nullptr) {
        /* It's a heap type, so needs the heap types' dealloc.
           subtype_dealloc will call the base type's tp_dealloc, if
           necessary. */
        type->tp_dealloc = _PyPy_subtype_dealloc;
    }

    /// Here is the only change needed: Do not finalize type creation.
    // if (PyType_Ready(type) < 0)
    //     goto fail;
    PepType_SetDict(type, PyDict_New());
    /// This is not found in PyPy:
    // if (type->tp_dictoffset) {
    //     res->ht_cached_keys = _PyDict_NewKeysForClass();
    // }

    /* Set type.__module__ */
    /// Removed __module__ handling, already implemented.

    return (PyObject*)res;

 fail:
    Py_DECREF(res);
    return nullptr;
}

#endif // PYPY_VERSION

} //extern "C"
