// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

////////////////////////////////////////////////////////////////////////////
//
// signature_extend.cpp
// --------------------
//
// This file contains the additions and changes to the following
// Python types:
//
//     PyMethodDescr_Type
//     PyCFunction_Type
//     PyStaticMethod_Type
//     (*) PyType_Type
//     PyWrapperDescr_Type
//
// Their `tp_getset` fields are modified to support the `__signature__`
// attribute and additions to the `__doc__` attribute.
//
// PYSIDE-535: PyType_Type patching is removed,
//             Shiboken.ObjectType and Shiboken.EnumMeta have new getsets, instead.

#include "autodecref.h"
#include "sbkpep.h"
#ifdef Py_GIL_DISABLED
#  include "sbkfailpoint.h"
#  include "sbkftoptions.h"
#endif
#include "sbkstring.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"

#include "signature_p.h"
#ifdef Py_GIL_DISABLED
#  include "sbkheldlocks.h"
#endif

#include <cstring>

using namespace Shiboken;

extern "C" {

using signaturefunc = PyObject *(*)(PyObject *, PyObject *);

static PyObject *_get_written_signature(signaturefunc sf, PyObject *ob, PyObject *modifier)
{
    /*
     * Be a writable Attribute, but have a computed value.
     *
     * If a signature has not been written, call the signature function.
     * If it has been written, return the written value.
     * After __del__ was called, the function value re-appears.
     *
     * Note: This serves also for the new version that does not allow any
     * assignment if we have a computed value. We only need to check if
     * a computed value exists and then forbid writing.
     * See pyside_set___signature
     */
    PyObject *ret = PyDict_GetItem(signatureGlobals()->value_dict, ob);
    if (ret == nullptr)
        return ob == nullptr ? nullptr : sf(ob, modifier);
    Py_INCREF(ret);
    return ret;
}

#ifdef PYPY_VERSION
PyObject *pyside_bm_get___signature__(PyObject *func, PyObject *modifier)
{
    return _get_written_signature(GetSignature_Method, func, modifier);
}
#endif

PyObject *pyside_cf_get___signature__(PyObject *func, PyObject *modifier)
{
    return _get_written_signature(GetSignature_Function, func, modifier);
}

PyObject *pyside_sm_get___signature__(PyObject *sm, PyObject *modifier)
{
    AutoDecRef func(PyObject_GetAttr(sm, PyMagicName::func()));
    return _get_written_signature(GetSignature_Function, func, modifier);
}

PyObject *pyside_md_get___signature__(PyObject *ob_md, PyObject *modifier)
{
    AutoDecRef func(name_key_to_func(ob_md));
    if (func.object() == Py_None)
        Py_RETURN_NONE;
    if (func.isNull())
        Py_FatalError("libshiboken: missing mapping in MethodDescriptor");
    return pyside_cf_get___signature__(func, modifier);
}

PyObject *pyside_wd_get___signature__(PyObject *ob, PyObject *modifier)
{
    return _get_written_signature(GetSignature_Wrapper, ob, modifier);
}

PyObject *pyside_tp_get___signature__(PyObject *obtype_mod, PyObject *modifier)
{
    return _get_written_signature(GetSignature_TypeMod, obtype_mod, modifier);
}

////////////////////////////////////////////////////////////////////////////
//
// Augmenting builtin types with a __signature__ attribute.
//
// This is a harmless change to Python, similar like __text_signature__.
// We could avoid it, but then we would need to copy quite some module
// initialization functions which are pretty version- and word size
// dependent. I think this little patch is the lesser of the two evils.
//
// Please note that in fact we are modifying 'type', the metaclass of all
// objects, because we add new functionality.
//
// Addendum 2019-01-12: We now also compute a docstring from the signature.
//

// keep the original __doc__ functions
static PyObject *old_cf_doc_descr = nullptr;
static PyObject *old_sm_doc_descr = nullptr;
static PyObject *old_md_doc_descr = nullptr;
static PyObject *old_tp_doc_descr = nullptr;
static PyObject *old_wd_doc_descr = nullptr;

#ifdef Py_GIL_DISABLED
// Per thread: two threads inside make_helptext() saw each other's recursion
static thread_local int handle_doc_depth = 0;
static int handle_doc_depth_shared = 0;

static int &handleDocDepth()
{
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::DocRecursionPerThread)) {
        return handle_doc_depth_shared;
    }
    return handle_doc_depth;
}
#else // Py_GIL_DISABLED
static int handle_doc_in_progress = 0;
#endif // Py_GIL_DISABLED

static PyObject *handle_doc(PyObject *ob, PyObject *old_descr)
{
    AutoDecRef ob_type_mod(GetClassOrModOf(ob));
    bool isModule = PyModule_Check(ob_type_mod.object());
    const char *name = isModule
        ? PyModule_GetName(ob_type_mod.object())
        : PepType_GetFullyQualifiedNameStr(reinterpret_cast<PyTypeObject *>(ob_type_mod.object()));
    PyObject *res{};

#ifdef Py_GIL_DISABLED
    int &depth = handleDocDepth();
    if (depth != 0 || name == nullptr
        || (isModule && std::strncmp(name, "PySide6.", 8) != 0)) {
        res = PyObject_CallMethodObjArgs(old_descr, PyMagicName::get(), ob, nullptr);
    } else {
        ++depth;
        Shiboken::failpoint("doc-before-helptext");
        res = PyObject_CallFunction(signatureGlobals()->make_helptext_func, "(O)", ob);
        --depth;
    }
#else
    if (handle_doc_in_progress || name == nullptr
        || (isModule && std::strncmp(name, "PySide6.", 8) != 0)) {
        res = PyObject_CallMethodObjArgs(old_descr, PyMagicName::get(), ob, nullptr);
    } else {
        handle_doc_in_progress++;
        res = PyObject_CallFunction(signatureGlobals()->make_helptext_func, "(O)", ob);
        handle_doc_in_progress--;
    }

#endif // Py_GIL_DISABLED

    if (res)
        return res;

    PyErr_Clear();
    Py_RETURN_NONE;
}

static PyObject *pyside_cf_get___doc__(PyObject *cf, void *)
{
    return handle_doc(cf, old_cf_doc_descr);
}

static PyObject *pyside_sm_get___doc__(PyObject *sm, void *)
{
    return handle_doc(sm, old_sm_doc_descr);
}

static PyObject *pyside_md_get___doc__(PyObject *md, void *)
{
    return handle_doc(md, old_md_doc_descr);
}

PyObject *pyside_tp_get___doc__(PyObject *tp)
{
    return handle_doc(tp, old_tp_doc_descr);
}

static PyObject *pyside_wd_get___doc__(PyObject *wd, void *)
{
    return handle_doc(wd, old_wd_doc_descr);
}

// PYSIDE-535: We cannot patch types easily in PyPy.
//             Let's use the `get_signature` function, instead.
static PyGetSetDef new_PyCFunction_getsets[] = {
    {"__doc__", pyside_cf_get___doc__, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

static PyGetSetDef new_PyStaticMethod_getsets[] = {
    {"__doc__", pyside_sm_get___doc__, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

static PyGetSetDef new_PyMethodDescr_getsets[] = {
    {"__doc__", pyside_md_get___doc__, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

static PyGetSetDef new_PyWrapperDescr_getsets[] = {
    {"__doc__", pyside_wd_get___doc__, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

int PySide_PatchTypes(void)
{
    static int init_done = 0;
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceFlag once;
    Shiboken::OnceGate gate(once, [] { return init_done != 0; });
#endif

    if (!init_done) {
        AutoDecRef meth_descr(PyObject_GetAttrString(
                                    reinterpret_cast<PyObject *>(&PyUnicode_Type), "split"));
        AutoDecRef wrap_descr(PyObject_GetAttrString(
                                    reinterpret_cast<PyObject *>(Py_TYPE(Py_True)), "__add__"));
        // abbreviations for readability
        auto *md_gs = new_PyMethodDescr_getsets;
        auto *md_doc = &old_md_doc_descr;
        auto *cf_gs = new_PyCFunction_getsets;
        auto *cf_doc = &old_cf_doc_descr;
        auto *sm_gs = new_PyStaticMethod_getsets;
        auto *sm_doc = &old_sm_doc_descr;
        auto *wd_gs = new_PyWrapperDescr_getsets;
        auto *wd_doc = &old_wd_doc_descr;

        if (meth_descr.isNull() || wrap_descr.isNull()
            || PyType_Ready(Py_TYPE(meth_descr)) < 0
            || add_more_getsets(PepMethodDescr_TypePtr,  md_gs, md_doc) < 0
            || add_more_getsets(&PyCFunction_Type,       cf_gs, cf_doc) < 0
            || add_more_getsets(PepStaticMethod_TypePtr, sm_gs, sm_doc) < 0
            || add_more_getsets(Py_TYPE(wrap_descr),     wd_gs, wd_doc) < 0
            )
            return -1;
        init_done = 1;
    }
    return 0;
}

} // extern "C"
