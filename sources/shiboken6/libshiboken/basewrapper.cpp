// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:low-level-memory-management

#include "autodecref.h"
#include "basewrapper.h"
#include "basewrapper_p.h"
#include "bindingmanager.h"
#include "gilstate.h"
#include "helper.h"
#include "pep384ext.h"
#include "pep384impl_p.h"
#include "sbkconverter.h"
#include "sbkerrors.h"
#include "sbkfeature_base.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"
#include "sbkstring.h"
#include "sbktypefactory.h"
#include "signature.h"
#include "signature_p.h"
#include "threadstatesaver.h"
#include "voidptr.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#ifdef __APPLE__
#  include <dlfcn.h>
#endif

namespace {
    void _destroyParentInfo(SbkObject *obj, bool keepReference);
    void _detachChildren(SbkObject *obj, bool keepReference);
}

struct BaseWrapperGlobals
{
    PyTypeObject *sbkObjectType = nullptr;
    PyTypeObject *sbkObjectMetaType = nullptr;
    PyObject *qAppLast = nullptr;
};

static BaseWrapperGlobals *baseWrapperGlobals()
{
    static BaseWrapperGlobals result;
    return &result;
}

static inline void setBaseClassIndexError(PyTypeObject *type, PyTypeObject *desiredType)
{
    PyErr_Format(PyExc_RuntimeError,
                 "libshiboken: \"%s\" is not a base class of \"%s\".",
                 desiredType->tp_name, type->tp_name);
}

static inline void setAlreadyInitializedError(PyTypeObject *type, PyTypeObject *desiredType)
{
    PyErr_Format(PyExc_RuntimeError,
                 "libshiboken: You can't initialize an \"%s\" object in class \"%s\" twice!",
                 desiredType->tp_name, type->tp_name);
}

namespace Shiboken
{
// Walk through the first level of non-user-type Sbk base classes relevant for
// C++ object allocation. Return true from the predicate to terminate.
template <class Predicate>
bool walkThroughBases(PyTypeObject *currentType, Predicate predicate)
{
    PyObject *bases = currentType->tp_bases;
    const Py_ssize_t numBases = PyTuple_Size(bases);
    bool result = false;
    for (Py_ssize_t i = 0; !result && i < numBases; ++i) {
        auto *type = reinterpret_cast<PyTypeObject *>(PyTuple_GetItem(bases, i));
        if (PyType_IsSubtype(type, SbkObject_TypeF()) != 0) {
            result = PepType_SOTP(type)->is_user_type
                     ? walkThroughBases(type, predicate) : predicate(type);
        }
    }
    return result;
}

static unsigned getTypeIndexOnHierarchy(PyTypeObject *baseType, PyTypeObject *desiredType)
{
    unsigned index = 0;
    walkThroughBases(baseType, [&index, desiredType](PyTypeObject *node) {
        if (PyType_IsSubtype(node, desiredType) != 0)
            return true;
        ++index;
        return false;
    });
    return index;
}

unsigned getNumberOfCppBaseClasses(PyTypeObject *baseType)
{
    unsigned count = 0;
    walkThroughBases(baseType, [&count](PyTypeObject *) {
        ++count;
        return false;
    });
    return count;
}

std::vector<PyTypeObject *> getCppBaseClasses(PyTypeObject *baseType)
{
    std::vector<PyTypeObject *> cppBaseClasses;
    walkThroughBases(baseType, [&cppBaseClasses](PyTypeObject *node) {
        cppBaseClasses.push_back(node);
        return false;
    });
    return cppBaseClasses;
}

using DestructorEntries = std::vector<DestructorEntry>;

DestructorEntries getDestructorEntries(SbkObject *o)
{
    DestructorEntries result;
    const auto cptrs = Shiboken::cppPointersSpan(o);
    walkThroughBases(Shiboken::pyType(o), [&result, &cptrs](PyTypeObject *node) {
        auto *sotp = PepType_SOTP(node);
        auto index = result.size();
        result.push_back(DestructorEntry{sotp->cpp_dtor,
                                         cptrs[index]});
        return false;
    });
    return result;
}

static void callDestructor(const DestructorEntries &dts)
{
    for (const auto &e : dts) {
        Shiboken::ThreadStateSaver threadSaver;
        threadSaver.save();
        e.destructor(e.cppInstance);
    }
}

} // namespace Shiboken

extern "C"
{

// PYSIDE-939: A general replacement for object_dealloc.
void Sbk_object_dealloc(PyObject *self)
{
    // PYSIDE-939: Handling references correctly.
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(Py_TYPE(self));

    PepExt_TypeCallFree(self);
}

static void SbkObjectType_tp_dealloc(PyTypeObject *type);
static PyTypeObject *SbkObjectType_tp_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds);

static DestroyQAppHook DestroyQApplication = nullptr;

// PYSIDE-1470: Provide a hook to kill an Application from Shiboken.
void setDestroyQApplication(DestroyQAppHook func)
{
    DestroyQApplication = func;
}

// PYSIDE-535: Use the C API in PyPy instead of `op->ob_dict`, directly
LIBSHIBOKEN_API PyObject *SbkObject_GetDict_NoRef(PyObject *op)
{
    assert(Shiboken::Object::checkType(op));
#ifdef PYPY_VERSION
    Shiboken::GilState state;
    auto *ret = PyObject_GenericGetDict(op, nullptr);
    Py_DECREF(ret);
    return ret;
#else
    auto *sbkObj = reinterpret_cast<SbkObject *>(op);
    if (!sbkObj->ob_dict) {
        Shiboken::GilState state;
        sbkObj->ob_dict = PyDict_New();
    }
    return sbkObj->ob_dict;
#endif
}

static int
check_set_special_type_attr(PyTypeObject *type, PyObject *value, const char *name)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "can't set %s.%s", type->tp_name, name);
        return 0;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "can't delete %s.%s", type->tp_name, name);
        return 0;
    }
    return 1;
}

// PYSIDE-1177: Add a setter to allow setting type doc.
static int
type_set_doc(PyObject *obType, PyObject *value, void * /* context */)
{
    auto *type = reinterpret_cast<PyTypeObject *>(obType);
    if (!check_set_special_type_attr(type, value, "__doc__"))
        return -1;
    PyType_Modified(type);
    Shiboken::AutoDecRef tpDict(PepType_GetDict(type));
    return PyDict_SetItem(tpDict.object(), Shiboken::PyMagicName::doc(), value);
}

// PYSIDE-908: The function PyType_Modified does not work in PySide, so we need to
// explicitly pass __doc__.
static PyGetSetDef SbkObjectType_tp_getset[] = {
    {"__doc__",  Sbk_TypeGet___doc__, type_set_doc, nullptr, nullptr},
    {"__dict__",  Sbk_TypeGet___dict__, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}  // Sentinel
};

static PyTypeObject *createObjectTypeType()
{
    PyType_Slot SbkObjectType_Type_slots[] = {
        {Py_tp_dealloc, reinterpret_cast<void *>(SbkObjectType_tp_dealloc)},
        {Py_tp_getattro, reinterpret_cast<void *>(mangled_type_getattro)},
        {Py_tp_base, static_cast<void *>(&PyType_Type)},
        {Py_tp_alloc, reinterpret_cast<void *>(PyType_GenericAlloc)},
        {Py_tp_new, reinterpret_cast<void *>(SbkObjectType_tp_new)},
        {Py_tp_free, reinterpret_cast<void *>(PyObject_GC_Del)},
        {Py_tp_getset, reinterpret_cast<void *>(SbkObjectType_tp_getset)},
        {0, nullptr}
    };

    // PYSIDE-535: The tp_itemsize field is inherited and does not need to be set.
    //             In PyPy, it _must_ not be set, because it would have the meanin
    //             that a `__len__` field must be defined. Not doing so creates
    //             a hard-to-find crash.
    //
    // PYSIDE-2230: In Python < 3.12, the decision which base class should create
    //              the instance is arbitrarily drawn by the size of the type.
    //              Ignoring this creates a bug in the new version of bug_825 that
    //              selects the wrong metatype.
    //
    PyType_Spec SbkObjectType_Type_spec = {
#if SHIBOKEN_MAJOR_VERSION >= 7 // PYSIDE-3336
        "2:shiboken6.Shiboken.ObjectType",
#else
        "1:Shiboken.ObjectType",
#endif
        static_cast<int>(PyType_Type.tp_basicsize) + 1,           // see above
        0, // sizeof(PyMemberDef), not for PyPy without a __len__ defined
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_TYPE_SUBCLASS,
        SbkObjectType_Type_slots,
    };

    PyType_Spec SbkObjectType_Type_spec_312 = {
#if SHIBOKEN_MAJOR_VERSION >= 7 // PYSIDE-3336
        "2:shiboken6.Shiboken.ObjectType",
#else
        "1:Shiboken.ObjectType",
#endif
        -long(sizeof(SbkObjectTypePrivate)),
        0, // sizeof(PyMemberDef), not for PyPy without a __len__ defined
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_TYPE_SUBCLASS,
        SbkObjectType_Type_slots,
    };

    return SbkType_FromSpec(_PepRuntimeVersion() >= 0x030C00 ?
                            &SbkObjectType_Type_spec_312 :
                            &SbkObjectType_Type_spec);
}

PyTypeObject *SbkObjectType_TypeF(void)
{
    auto *globals = baseWrapperGlobals();
    if (globals->sbkObjectMetaType == nullptr)
        globals->sbkObjectMetaType = createObjectTypeType();
    return globals->sbkObjectMetaType;
}

static PyObject *SbkObjectGetDict(PyObject *pObj, void *)
{
    auto *ret = SbkObject_GetDict_NoRef(pObj);
    Py_XINCREF(ret);
    return ret;
}

static PyGetSetDef SbkObject_tp_getset[] = {
    {const_cast<char *>("__dict__"), SbkObjectGetDict, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr} // Sentinel
};

static int SbkObject_tp_traverse(PyObject *self, visitproc visit, void *arg)
{
    auto *sbkSelf = reinterpret_cast<SbkObject *>(self);

    //Visit children
    if (auto *pInfo = sbkSelf->d->parentInfo) {
        for (SbkObject *c : pInfo->children)
             Py_VISIT(c);
    }

    //Visit refs
    if (auto *rInfo = sbkSelf->d->referredObjects) {
        for (const auto &p : *rInfo)
            Py_VISIT(p.second);
    }

    if (sbkSelf->ob_dict)
        Py_VISIT(sbkSelf->ob_dict);

    // This was not needed before Python 3.9 (Python issue 35810 and 40217)
    Py_VISIT(Py_TYPE(self));
    return 0;
}

static int SbkObject_tp_clear(PyObject *self)
{
    auto *sbkSelf = reinterpret_cast<SbkObject *>(self);

    // Only the children are detached here, never the link to our own C++
    // parent: that parent still holds this object in its C++ child list and
    // deletes it, so handing ownership back to Python would make the wrapper
    // delete it a second time. The reference cycle is broken by clearing the
    // instance dict below, and the parent link is torn down when the parent
    // itself is cleared or deallocated.
    if (sbkSelf->d->parentInfo)
        _detachChildren(sbkSelf, true);

    Shiboken::Object::clearReferences(sbkSelf);

    if (sbkSelf->ob_dict)
        Py_CLEAR(sbkSelf->ob_dict);
    return 0;
}

static PyTypeObject *createObjectType()
{
    PyType_Slot SbkObject_Type_slots[] = {
        {Py_tp_getattro, reinterpret_cast<void *>(SbkObject_GenericGetAttr)},
        {Py_tp_setattro, reinterpret_cast<void *>(SbkObject_GenericSetAttr)},
        {Py_tp_dealloc, reinterpret_cast<void *>(SbkDeallocWrapperWithPrivateDtor)},
        {Py_tp_traverse, reinterpret_cast<void *>(SbkObject_tp_traverse)},
        {Py_tp_clear, reinterpret_cast<void *>(SbkObject_tp_clear)},
        // unsupported: {Py_tp_weaklistoffset, (void *)offsetof(SbkObject, weakreflist)},
        {Py_tp_getset, reinterpret_cast<void *>(SbkObject_tp_getset)},
        // unsupported: {Py_tp_dictoffset, (void *)offsetof(SbkObject, ob_dict)},
        {0, nullptr}
    };

    PyType_Spec SbkObject_Type_spec = {
#if SHIBOKEN_MAJOR_VERSION >= 7 // PYSIDE-3336
        "2:shiboken6.Shiboken.Object",
#else
        "1:Shiboken.Object",
#endif
        sizeof(SbkObject),
        0,
        Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
        SbkObject_Type_slots,
    };

    // PYSIDE-2230: When creating this type, we cannot easily handle the metaclass.
    //              In versions < Python 3.12, the metaclass can only be set
    //              indirectly by a base which has that metaclass.
    //              But before 3.12 is the minimum version, we cannot use the new
    //              function, although we would need this for 3.12 :-D
    //              We do a special patching here that is triggered through Py_None.
    auto *type = SbkType_FromSpec_BMDWB(&SbkObject_Type_spec,
                                        Py_None,     // bases, spectial flag!
                                        SbkObjectType_TypeF(),
                                        offsetof(SbkObject, ob_dict),
                                        offsetof(SbkObject, weakreflist),
                                        nullptr);    // bufferprocs
    return type;
}

PyTypeObject *SbkObject_TypeF(void)
{
    auto *globals = baseWrapperGlobals();
    if (globals->sbkObjectType == nullptr)
        globals->sbkObjectType = createObjectType();    // bufferprocs
    return globals->sbkObjectType;
}

static const char *SbkObject_SignatureStrings[] = {
    "Shiboken.Object(self)",
    nullptr}; // Sentinel

static int mainThreadDeletionHandler(void *)
{
    if (Py_IsInitialized())
        Shiboken::BindingManager::instance().runDeletionInMainThread();
    return 0;
}

static void SbkDeallocWrapperCommon(PyObject *pyObj, bool canDelete)
{
    auto *sbkObj = reinterpret_cast<SbkObject *>(pyObj);
    PyTypeObject *pyType = Py_TYPE(pyObj);

    // Need to decref the type if this is the dealloc func; if type
    // is subclassed, that dealloc func will decref (see subtype_dealloc
    // in typeobject.c in the python sources)
    auto *dealloc = PyType_GetSlot(pyType, Py_tp_dealloc);

    // PYSIDE-939: Additional rule: Also when a subtype is heap allocated,
    // then the subtype_dealloc deref will be suppressed, and we need again
    // to supply a decref.
    const bool needTypeDecref = dealloc == SbkDeallocWrapper
        || dealloc == SbkDeallocWrapperWithPrivateDtor
        || (pyType->tp_base->tp_flags & Py_TPFLAGS_HEAPTYPE) != 0;

#ifdef __APPLE__
    // Just checking once that our assumptions are right.
    if (false) {
        void *p = PyType_GetSlot(pyType, Py_tp_dealloc);
        Dl_info dl_info;
        dladdr(p, &dl_info);
        fprintf(stderr, "tp_dealloc is %s\n", dl_info.dli_sname);
    }
    // Gives one of our functions
    //  "Sbk_object_dealloc"
    //  "SbkDeallocWrapperWithPrivateDtor"
    //  "SbkDeallocQAppWrapper"
    //  "SbkDeallocWrapper"
    // but for typedealloc_test.py we get
    //  "subtype_dealloc"
#endif

    // Ensure that the GC is no longer tracking this object to avoid a
    // possible reentrancy problem.  Since there are multiple steps involved
    // in deallocating a SbkObject it is possible for the garbage collector to
    // be invoked and it trying to delete this object while it is still in
    // progress from the first time around, resulting in a double delete and a
    // crash.
    PyObject_GC_UnTrack(pyObj);

    // Take the object out of the wrapper map before anything below can run
    // Python code. The refcount is already zero here, but the map used to keep
    // the entry until deallocData(), several steps down. A lookup in between -
    // a weakref callback, a __del__, C++ re-entering the binding - was handed
    // this very wrapper and increfed it back to life, while the deallocation
    // carried on and freed it underneath. The invariant callers rely on is
    // "in the map implies alive", and this is where it has to be restored.
    // Only the entry goes; the flags stay, canDelete below still reads them.
    auto &bindingManager = Shiboken::BindingManager::instance();
    bindingManager.unregisterWrapper(sbkObj);

    // Check that Python is still initialized as sometimes this is called by a static destructor
    // after Python interpeter is shutdown.
    if (sbkObj->weakreflist && Py_IsInitialized())
        PyObject_ClearWeakRefs(pyObj);

    // If I have ownership and is valid delete C++ pointer
    auto *sotp = PepType_SOTP(pyType);
    canDelete &= sbkObj->d->hasOwnership && sbkObj->d->validCppObject;
    if (canDelete) {
        if (sotp->delete_in_main_thread && Shiboken::currentThreadId() != Shiboken::mainThreadId()) {
            if (sotp->is_multicpp) {
                 const auto entries = Shiboken::getDestructorEntries(sbkObj);
                 for (const auto &e : entries)
                     bindingManager.addToDeletionInMainThread(e);
            } else {
                Shiboken::DestructorEntry e{sotp->cpp_dtor, Shiboken::cppPointer(sbkObj->d)};
                bindingManager.addToDeletionInMainThread(e);
            }
            Py_AddPendingCall(mainThreadDeletionHandler, nullptr);
            canDelete = false;
        }
    }

    /* Save the current exception, if any. */
    Shiboken::Errors::Stash errorStash;

    if (canDelete) {
        if (sotp->is_multicpp) {
            const auto entries = Shiboken::getDestructorEntries(sbkObj);
            Shiboken::Object::deallocData(sbkObj, true);
            callDestructor(entries);
        } else {
            void *cptr = Shiboken::cppPointer(sbkObj->d);
            Shiboken::Object::deallocData(sbkObj, true);

            Shiboken::ThreadStateSaver threadSaver;
            if (Py_IsInitialized())
                threadSaver.save();
            sotp->cpp_dtor(cptr);
        }
    } else {
        Shiboken::Object::deallocData(sbkObj, true);
    }

    /* Restore the saved exception. */
    errorStash.restore();

    if (needTypeDecref)
        Py_DECREF(pyType);
    // PYSIDE-939: Handling references correctly.
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(pyType);
}

static inline PyObject *_Sbk_NewVarObject(PyTypeObject *type)
{
    // PYSIDE-1970: Support __slots__, implemented by PyVarObject
    auto const baseSize = sizeof(SbkObject);
    auto varCount = Py_SIZE(reinterpret_cast<PyObject *>(type));
    auto *self = PyObject_GC_NewVar(PyObject, type, varCount);
    if (varCount)
        std::memset(reinterpret_cast<char *>(self) + baseSize, 0, varCount * sizeof(void *));
    return self;
}

void SbkDeallocWrapper(PyObject *pyObj)
{
    SbkDeallocWrapperCommon(pyObj, true);
}

void SbkDeallocQAppWrapper(PyObject *pyObj)
{
    SbkDeallocWrapper(pyObj);
    // PYSIDE-571: make sure to create a singleton deleted qApp.
    Py_DECREF(MakeQAppWrapper(nullptr));
}

void SbkDeallocWrapperWithPrivateDtor(PyObject *self)
{
    SbkDeallocWrapperCommon(self, false);
}

void SbkObjectType_tp_dealloc(PyTypeObject *type)
{
    SbkObjectTypePrivate *sotp = PepType_SOTP(type);
    auto *pyObj = reinterpret_cast<PyObject *>(type);

    PyObject_GC_UnTrack(pyObj);
#if !defined(Py_LIMITED_API) && !defined(PYPY_VERSION)
    Py_TRASHCAN_BEGIN(pyObj, 1);
#endif
    if (sotp) {
        if (sotp->user_data && sotp->d_func) {
            sotp->d_func(sotp->user_data);
            sotp->user_data = nullptr;
        }
        free(sotp->original_name);
        sotp->original_name = nullptr;
        if (!Shiboken::ObjectType::isUserType(type))
            Shiboken::Conversions::deleteConverter(sotp->converter);
        PepType_SOTP_delete(type);
    }
#if !defined(Py_LIMITED_API) && !defined(PYPY_VERSION)
    Py_TRASHCAN_END;
#endif
    // PYSIDE-939: Handling references correctly.
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(Py_TYPE(pyObj));
}

////////////////////////////////////////////////////////////////////////////
//
// Support for the qApp macro.
//
// qApp is a macro in Qt5. In Python, we simulate that a little by a
// variable that monitors Q*Application.instance().
// This variable is also able to destroy the app by qApp.shutdown().
//

PyObject *MakeQAppWrapper(PyTypeObject *type)
{
    PyObject *&qApp_last = baseWrapperGlobals()->qAppLast;

    // protecting from multiple application instances
    if (type != nullptr && qApp_last != Py_None) {
        const char *res_name = qApp_last != nullptr
            ? PepType_GetNameStr(Py_TYPE(qApp_last)) : "<Unknown>";
        const char *type_name = PepType_GetNameStr(type);
        PyErr_Format(PyExc_RuntimeError,
                     "libshiboken: Please destroy the %s singleton before"
                     " creating a new %s instance.", res_name, type_name);
        return nullptr;
    }

    // monitoring the last application state
    PyObject *qApp_curr = type != nullptr ? _Sbk_NewVarObject(type) : Py_None;
    Shiboken::AutoDecRef builtins(PepEval_GetFrameBuiltins());
    if (PyDict_SetItem(builtins.object(), Shiboken::PyName::qApp(), qApp_curr) < 0)
        return nullptr;
    builtins.reset(nullptr);
    qApp_last = qApp_curr;
    // Note: This Py_INCREF would normally be wrong because the qApp
    // object already has a reference from PyObject_GC_New. But this is
    // exactly the needed reference that keeps qApp alive from alone!
    Py_INCREF(qApp_curr);
    // PYSIDE-1470: As a side effect, the interactive "_" variable tends to
    //              create reference cycles. This is disturbing when trying
    //              to remove qApp with del.
    // PYSIDE-1758: Since we moved to an explicit qApp.shutdown() call, we
    //              no longer initialize "_" with Py_None.
    return qApp_curr;
}

static PyTypeObject *SbkObjectType_tp_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    // Check if all bases are new style before calling type.tp_new
    // Was causing gc assert errors in test_bug704.py when
    // this check happened after creating the type object.
    // Argument parsing take from type.tp_new code.

    // PYSIDE-595: Also check if all bases allow inheritance.
    // Before we changed to heap types, it was sufficient to remove the
    // Py_TPFLAGS_BASETYPE flag. That does not work, because PySide does
    // not respect this flag itself!
    PyObject *name{};
    PyObject *pyBases{};
    PyObject *dict{};
    static const char *kwlist[] = { "name", "bases", "dict", nullptr};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO!O!:sbktype", const_cast<char **>(kwlist),
                                     &name,
                                     &PyTuple_Type, &pyBases,
                                     &PyDict_Type, &dict))
        return nullptr;

    for (Py_ssize_t i=0, i_max=PyTuple_Size(pyBases); i < i_max; i++) {
        PyObject *baseType = PyTuple_GetItem(pyBases, i);
        if (PepExt_Type_GetNewSlot(reinterpret_cast<PyTypeObject *>(baseType)) == SbkDummyNew) {
            // PYSIDE-595: A base class does not allow inheritance.
            return reinterpret_cast<PyTypeObject *>(SbkDummyNew(metatype, args, kwds));
        }
    }

    PyTypeObject *newType = PepType_Type_tp_new(metatype, args, kwds);

    if (!newType)
        return nullptr;

    SbkObjectTypePrivate *sotp = PepType_SOTP(newType);

    const auto bases = Shiboken::getCppBaseClasses(newType);
    if (bases.size() == 1) {
        SbkObjectTypePrivate *parentType = PepType_SOTP(bases.front());
        sotp->mi_offsets = parentType->mi_offsets;
        sotp->mi_init = parentType->mi_init;
        sotp->mi_specialcast = parentType->mi_specialcast;
        sotp->type_discovery = parentType->type_discovery;
        sotp->cpp_dtor = parentType->cpp_dtor;
        sotp->is_multicpp = 0;
        sotp->converter = parentType->converter;
    } else {
        sotp->mi_offsets = nullptr;
        sotp->mi_init = nullptr;
        sotp->mi_specialcast = nullptr;
        sotp->type_discovery = nullptr;
        sotp->cpp_dtor = nullptr;
        sotp->is_multicpp = 1;
        sotp->converter = nullptr;
    }
    if (bases.size() == 1) {
        const char *original_name = PepType_SOTP(bases.front())->original_name;
        if (original_name == nullptr)
            original_name = "object";
        sotp->original_name = strdup(original_name);
    }
    else
        sotp->original_name = strdup("object");
    sotp->user_data = nullptr;
    sotp->d_func = nullptr;
    sotp->is_user_type = 1;

    // PYSIDE-1463: Prevent feature switching while in the creation process
    auto saveFeature = initSelectableFeature(nullptr);
    for (PyTypeObject *base : bases) {
        auto *baseSotp = PepType_SOTP(base);
        if (baseSotp->subtype_init)
            baseSotp->subtype_init(newType, args, kwds);
        if (baseSotp->delete_in_main_thread)
            sotp->delete_in_main_thread = 1;
    }
    initSelectableFeature(saveFeature);
    return newType;
}

static PyObject *_setupNew(PyObject *obSelf, PyTypeObject *subtype)
{
    auto *obSubtype = reinterpret_cast<PyObject *>(subtype);
    auto *sbkSubtype = subtype;
    auto *self = reinterpret_cast<SbkObject *>(obSelf);

    Py_INCREF(obSubtype);

    auto *sotp = PepType_SOTP(sbkSubtype);
    const bool is_multicpp = sotp != nullptr && sotp->is_multicpp;
    const unsigned numBases = is_multicpp ? Shiboken::getNumberOfCppBaseClasses(subtype) : 1U;
    assert(numBases >= 1);

    SbkObjectPrivate *d = nullptr;
    if (is_multicpp)
        d = new SbkObjectMultiInheritancePrivate(numBases);
    else
        d = new SbkObjectPrivate1;

    d->hasOwnership = 1;
    d->containsCppWrapper = 0;
    d->validCppObject = 0;
    d->parentInfo = nullptr;
    d->referredObjects = nullptr;
    d->cppObjectCreated = 0;
    d->isQAppSingleton = 0;
    d->is_multicpp = is_multicpp;
    self->ob_dict = nullptr;
    self->weakreflist = nullptr;
    self->d = d;
    PyObject_GC_Track(obSelf);
    return obSelf;
}

PyObject *SbkObject_tp_new(PyTypeObject *subtype, PyObject * /* args */, PyObject * /* kwds */)
{
    PyObject *self = _Sbk_NewVarObject(subtype);
    return _setupNew(self, subtype);
}

PyObject *SbkQApp_tp_new(PyTypeObject *subtype, PyObject *, PyObject *)
{
    auto *obSelf = MakeQAppWrapper(subtype);
    auto *self = reinterpret_cast<SbkObject *>(obSelf);
    if (self == nullptr)
        return nullptr;
    auto *ret = _setupNew(obSelf, subtype);
    self->d->isQAppSingleton = 1;
    return ret;
}

PyObject *SbkDummyNew(PyTypeObject *type, PyObject *, PyObject *)
{
    // PYSIDE-595: Give the same error as type_call does when tp_new is NULL.
    const char regret[] = "¯\\_(ツ)_/¯";
    PyErr_Format(PyExc_TypeError,
                 "cannot create '%.100s' instances %s", type->tp_name, regret);
    return nullptr;
}

// PYSIDE-74: Fallback used in all types now.
PyObject *FallbackRichCompare(PyObject *self, PyObject *other, int op)
{
    // This is a very simple implementation that supplies a simple identity.
    static const char * const opstrings[] = {"<", "<=", "==", "!=", ">", ">="};
    PyObject *res{};

    switch (op) {

    case Py_EQ:
        res = (self == other) ? Py_True : Py_False;
        break;
    case Py_NE:
        res = (self != other) ? Py_True : Py_False;
        break;
    default:
        PyErr_Format(PyExc_TypeError,
                     "'%s' not supported between instances of '%.100s' and '%.100s'",
                     opstrings[op],
                     self->ob_type->tp_name,
                     other->ob_type->tp_name);
        return nullptr;
    }
    Py_INCREF(res);
    return res;
}

bool SbkObjectType_Check(PyTypeObject *type)
{
    auto *meta = SbkObjectType_TypeF();
    auto *obType = reinterpret_cast<PyObject *>(type);
    return Py_TYPE(obType) == meta || PyType_IsSubtype(Py_TYPE(obType), meta);
}

// Global functions from folding.

// The common end.
PyObject *Sbk_ReturnFromPython_None()
{
    if (Shiboken::Errors::occurred() != nullptr) {
        return {};
    }
    Py_RETURN_NONE;
}

PyObject *Sbk_ReturnFromPython_Result(PyObject *pyResult)
{
    if (Shiboken::Errors::occurred() != nullptr || pyResult == nullptr) {
        Py_XDECREF(pyResult);
        return {};
    }
    return pyResult;
}

PyObject *Sbk_ReturnFromPython_Self(PyObject *self)
{
    if (Shiboken::Errors::occurred() != nullptr) {
        return {};
    }
    Py_INCREF(self);
    return self;
}

} //extern "C"

// Determine name of a Python override of a virtual method according to features
// and populate name cache.
static PyObject *overrideMethodName(PyObject *pySelf, const char *methodName,
                                    PyObject **nameCache)
{
    // PYSIDE-1626: Touch the type to initiate switching early.
    auto *obType = Py_TYPE(pySelf);
    SbkObjectType_UpdateFeature(obType);

    const int flag = currentSelectId(obType);
    const int propFlag = isdigit(methodName[0]) ? methodName[0] - '0' : 0;
    const bool is_snake = flag & 0x01;
    PyObject *pyMethodName = nameCache[is_snake];  // borrowed
    if (pyMethodName == nullptr) {
        if (propFlag)
            methodName += 2; // skip the propFlag and ':'
        pyMethodName = Shiboken::String::getSnakeCaseName(methodName, is_snake);
        nameCache[is_snake] = pyMethodName;
    }
    return pyMethodName;
}

// The virtual function call
PyObject *Sbk_GetPyOverride(const void *voidThis, PyTypeObject *typeObject,
                            Shiboken::GilState &gil, const char *funcName,
                            PyObject *&resultCache, PyObject **nameCache)
{
    if (Py_IsInitialized() == 0 || resultCache == Py_None)
        return nullptr; // Bail out, execute C++ call (wrappers may outlive Python).

    auto &bindingManager = Shiboken::BindingManager::instance();
    SbkObject *wrapper = bindingManager.retrieveWrapper(voidThis, typeObject);
    // The refcount can be 0 if the object is dieing and someone called
    // a virtual method from the destructor
    if (wrapper == nullptr)
        return nullptr;
    auto *pySelf = reinterpret_cast<PyObject *>(wrapper);
    if (Py_REFCNT(pySelf) == 0)
        return nullptr;

    gil.acquire();
    if (resultCache == Py_None) { // PYSIDE 3246, some other thread may have determined the override
        gil.release();
        return nullptr;
    }

    if (resultCache != nullptr) // recreate the callable from function/self
        return PepExt_Type_CallDescrGet(resultCache, pySelf, nullptr);

    PyObject *pyMethodName = overrideMethodName(pySelf, funcName, nameCache);
    auto *wrapper_dict = SbkObject_GetDict_NoRef(pySelf);

    // Note: This special case was implemented for duck-punching, which happens
    // in the instance dict. It does not work with properties.
    // This is not cached to avoid leaking. FIXME PYSIDE 7: Remove (PYSIDE-2916)?
    if (PyObject *method = PyDict_GetItem(wrapper_dict, pyMethodName)) {
        Py_INCREF(method);
        return method;
    }

    auto *pyOverride = Shiboken::BindingManager::getOverride(wrapper, pyMethodName);
    if (pyOverride == nullptr) {
        resultCache = Py_None;
        Py_INCREF(resultCache);
        gil.release();
        return nullptr; // No override, execute C++ call
    }

    if (Shiboken::Errors::occurred() != nullptr) {
        // Give up.
        Py_XDECREF(pyOverride);
        resultCache = Py_None;
        Py_INCREF(resultCache);
        gil.release();
        return nullptr; // // Give up.
    }

    resultCache = pyOverride;
    // recreate the callable from function/self
    return PepExt_Type_CallDescrGet(resultCache, pySelf, nullptr);
}

namespace
{

// Invalidate and detach the children of obj. Their C++ instances belong to
// obj's C++ instance and are deleted by it, so no ownership is handed back.
void _detachChildren(SbkObject *obj, bool keepReference)
{
    Shiboken::ParentInfo *pInfo = obj->d->parentInfo;
    if (pInfo) {
        while(!pInfo->children.empty()) {
            SbkObject *first = *pInfo->children.begin();
            // Mark child as invalid
            Shiboken::Object::invalidate(first);
            Shiboken::Object::removeParent(first, false, keepReference);
        }
    }
}

void _destroyParentInfo(SbkObject *obj, bool keepReference)
{
    if (obj->d->parentInfo) {
        _detachChildren(obj, keepReference);
        Shiboken::Object::removeParent(obj, false);
    }
}

}

/*!
    \module libshiboken

    \title Shiboken utility library

    \brief Contains functionality for Python bindings.
 */

/*!
    \namespace Shiboken

    \brief Root namespace for everything that is not extern "C".

    \inmodule libshiboken
*/
namespace Shiboken
{

// Wrapper metatype and base type ----------------------------------------------------------

void _initMainThreadId(); // helper.cpp

static std::string msgFailedToInitializeType(const char *description)
{
    std::ostringstream stream;
    stream << "libshiboken: Failed to initialize " << description;
    if (PyErr_Occurred() != nullptr) {
        Shiboken::Errors::Stash stash;
        if (auto *str = PyObject_Str(stash.getException()))
            stream << ": " << Shiboken::String::toCString(str);
    }
    stream << '.';
    return stream.str();
}

namespace Conversions { void init(); }

void init()
{
    static bool shibokenAlreadInitialised = false;
    if (shibokenAlreadInitialised) // Leave guard in place until fully ported to multi phase init
        return;

    _initMainThreadId();

    Conversions::init();

    //Init private data
    Pep384_Init();

    auto *type = SbkObjectType_TypeF();
    if (type == nullptr || PyType_Ready(type) < 0)
        Py_FatalError(msgFailedToInitializeType("Shiboken.BaseWrapperType metatype").c_str());

    type = SbkObject_TypeF();
    if (type == nullptr || PyType_Ready(type) < 0)
        Py_FatalError(msgFailedToInitializeType("Shiboken.BaseWrapper type").c_str());

    VoidPtr::init();

    shibokenAlreadInitialised = true;
}

// PYSIDE-1415: Publish Shiboken objects.
// PYSIDE-1735: Initialize the whole Shiboken startup.
void initShibokenSupport(PyObject *module)
{
    auto *type = SbkObject_TypeF();
    auto *obType = reinterpret_cast<PyObject *>(type);
    Py_INCREF(obType);
    PepModule_AddType(module, type);

    // PYSIDE-1735: When the initialization was moved into Shiboken import, this
    //              Py_INCREF became necessary. No idea why.
    Py_INCREF(module);
    init_shibokensupport_module();

    if (InitSignatureStrings(type, SbkObject_SignatureStrings) < 0)
        Py_FatalError("libshiboken: Error in initShibokenSupport");
}

// setErrorAboutWrongArguments now gets overload info from the signature module.
// Info can be nullptr and contains extra info.
void setErrorAboutWrongArguments(PyObject *args, const char *funcName, PyObject *info,
                                 const char *className)
{
    if (className != nullptr) {
        std::string text = std::string(className);
        text += '.';
        text += funcName;
        SetError_Argument(args, text.c_str(), info);
        return;
    }
    SetError_Argument(args, funcName, info);
}

PyObject *returnWrongArguments(PyObject *args, const char *memberName, PyObject *info,
                               Module::TypeInitStruct initStruct)
{
    setErrorAboutWrongArguments(args, memberName, info, initStruct.fullName);
    return {};
}

PyObject *returnWrongArguments(PyObject *args, const char *memberName,
                               Module::TypeInitStruct initStruct)
{
    setErrorAboutWrongArguments(args, memberName, nullptr, initStruct.fullName);
    return {};
}

PyObject *returnWrongArguments(PyObject *args, const char *globalFuncName, PyObject *info)
{
    setErrorAboutWrongArguments(args, globalFuncName, info);
    return {};
}

PyObject *returnWrongArguments(PyObject *args, const char *globalFuncName)
{
    setErrorAboutWrongArguments(args, globalFuncName, nullptr);
    return {};
}

int returnWrongArguments_Zero(PyObject *args, const char *memberName, PyObject *info,
                              Module::TypeInitStruct initStruct)
{
    setErrorAboutWrongArguments(args, memberName, info, initStruct.fullName);
    return 0;
}

int returnWrongArguments_Zero(PyObject *args, const char *globalFuncName, PyObject *info)
{
    setErrorAboutWrongArguments(args, globalFuncName, info);
    return 0;
}

int returnWrongArguments_MinusOne(PyObject *args, const char *memberName, PyObject *info,
                                  Module::TypeInitStruct initStruct)
{
    setErrorAboutWrongArguments(args, memberName, info, initStruct.fullName);
    return -1;
}

int returnWrongArguments_MinusOne(PyObject *args, const char *memberName,
                                  Module::TypeInitStruct initStruct)
{
    setErrorAboutWrongArguments(args, memberName, nullptr, initStruct.fullName);
    return -1;
}

int returnWrongArguments_MinusOne(PyObject *args, const char *globalFuncName, PyObject *info)
{
    setErrorAboutWrongArguments(args, globalFuncName, info);
    return -1;
}

PyObject *returnFromRichCompare(PyObject *result)
{
    if (result && !PyErr_Occurred())
        return result;
    Shiboken::Errors::setOperatorNotImplemented();
    return {};
}

PyObject *checkInvalidArgumentCount(Py_ssize_t numArgs, Py_ssize_t minArgs, Py_ssize_t maxArgs)
{
    PyObject *result = nullptr;
    // for seterror_argument(), signature/errorhandler.py
    if (numArgs > maxArgs) {
        static PyObject *const tooMany = Shiboken::String::createStaticString(">");
        result = tooMany;
        Py_INCREF(result);
    } else if (numArgs < minArgs) {
        static PyObject *const tooFew = Shiboken::String::createStaticString("<");
        static PyObject *const noArgs = Shiboken::String::createStaticString("0");
        result = numArgs > 0 ? tooFew : noArgs;
        Py_INCREF(result);
    }
    return result;
}

template <class Function>
static void traverseSequence(PyObject *pyObj, Function func)
{
    if (pyObj == nullptr)
        return;

    // Check for list/tuple explicitly, suppressing classes implementing the sequence protocol
    // like QPolygon (PYSIDE-3423).
    if (PyTuple_Check(pyObj) != 0 || PyList_Check(pyObj) != 0) {
        for (Py_ssize_t i = 0, size = PySequence_Size(pyObj); i < size; ++i) {
            Shiboken::AutoDecRef item(PySequence_GetItem(pyObj, i));
            if (Object::checkType(item))
                func(reinterpret_cast<SbkObject *>(item.object()));
        }
    } else if (Object::checkType(pyObj)) {
        func(reinterpret_cast<SbkObject *>(pyObj));
    }
}

CPtrSpan cppPointersSpan(SbkObject *ob)
{
    assert(ob->d != nullptr);
    if (ob->d->is_multicpp) {
        auto &cptrs = static_cast<SbkObjectMultiInheritancePrivate *>(ob->d)->cptrs;
        return CPtrSpan{ cptrs.data(), cptrs.data() + cptrs.size() };
    }
    auto *d1 = static_cast<SbkObjectPrivate1 *>(ob->d);
    return CPtrSpan{ &d1->cptr, &d1->cptr + 1 };
}

void setCppPointer(SbkObject *ob, void *cptr)
{
    if (ob->d->is_multicpp)
        static_cast<SbkObjectMultiInheritancePrivate *>(ob->d)->cptrs[0] = cptr;
    else
        static_cast<SbkObjectPrivate1 *>(ob->d)->cptr = cptr;
}

void clearCppPointers(SbkObject *ob)
{
    if (ob->d->is_multicpp) {
        auto &cptrs = static_cast<SbkObjectMultiInheritancePrivate *>(ob->d)->cptrs;
        std::fill(cptrs.begin(), cptrs.end(), nullptr);
    } else {
        static_cast<SbkObjectPrivate1 *>(ob->d)->cptr = nullptr;
    }
}

template <class Iterator>
inline void decRefPyObjectList(Iterator i1, Iterator i2)
{
    for (; i1 != i2; ++i1)
        Py_DECREF(i1->second);
}

/*!
    \namespace Shiboken::ObjectType

    \brief Namespace for object type-related functionality.

    \inmodule libshiboken
*/
namespace ObjectType
{

bool checkType(PyTypeObject *type)
{
    return PyType_IsSubtype(type, SbkObject_TypeF()) != 0;
}

bool isUserType(PyTypeObject *type)
{
    return checkType(type) && PepType_SOTP(type)->is_user_type;
}

/*!
    Returns true if the constructor of \a ctorType can be called for an instance
    of type \a myType.
    \note This function sets a Python error when returning false.
*/
bool canCallConstructor(PyTypeObject *myType, PyTypeObject *ctorType)
{
    auto findBasePred = [ctorType](PyTypeObject *type) { return type == ctorType; };
    if (!walkThroughBases(myType, findBasePred)) {
        PyErr_Format(PyExc_TypeError,
                     "libshiboken: %s isn't a direct base class of %s", ctorType->tp_name, myType->tp_name);
        return false;
    }
    return true;
}

/*!
    Returns true if the \a type represents an object of a class with multiple
    inheritance in C++. When this occurs, the C++ pointer held by the
    Python wrapper will need to be cast when
    passed as a parameter that expects a type of its ancestry.

    This means a call to ObjectType::cast() is needed to obtain the correct
    C++ pointer for Python objects of type \a type.
*/
bool hasCast(PyTypeObject *type)
{
    return PepType_SOTP(type)->mi_specialcast != nullptr;
}

/*!
    Cast the C++ pointer held by a Python object \a obj of type \a sourceType,
    to a C++ pointer of a C++ class indicated by type \a pyTargetType
    and return the cast C++ pointer.
*/
void *cast(PyTypeObject *sourceType, SbkObject *obj, PyTypeObject *pyTargetType)
{
    auto *sotp = PepType_SOTP(sourceType);
    return sotp->mi_specialcast(Object::cppPointer(obj, pyTargetType), pyTargetType);
}

void setCastFunction(PyTypeObject *type, SpecialCastFunction func)
{
    auto *sotp = PepType_SOTP(type);
    sotp->mi_specialcast = func;
}

void setOriginalName(PyTypeObject *type, const char *name)
{
    auto *sotp = PepType_SOTP(type);
    if (sotp->original_name)
        free(sotp->original_name);
    sotp->original_name = strdup(name);
}

const char *getOriginalName(PyTypeObject *type)
{
    return PepType_SOTP(type)->original_name;
}

void setTypeDiscoveryFunctionV2(PyTypeObject *type, TypeDiscoveryFuncV2 func)
{
    PepType_SOTP(type)->type_discovery = func;
}

void copyMultipleInheritance(PyTypeObject *type, PyTypeObject *other)
{
    auto *sotp_type = PepType_SOTP(type);
    auto *sotp_other = PepType_SOTP(other);
    sotp_type->mi_init = sotp_other->mi_init;
    sotp_type->mi_offsets = sotp_other->mi_offsets;
    sotp_type->mi_specialcast = sotp_other->mi_specialcast;
}

void setMultipleInheritanceFunction(PyTypeObject *type, MultipleInheritanceInitFunction function)
{
    PepType_SOTP(type)->mi_init = function;
}

MultipleInheritanceInitFunction getMultipleInheritanceFunction(PyTypeObject *type)
{
    return PepType_SOTP(type)->mi_init;
}

void setDestructorFunction(PyTypeObject *type, ObjectDestructor func)
{
    PepType_SOTP(type)->cpp_dtor = func;
}

/*!
    Initializes a Shiboken wrapper type and adds it to the module,
    or to the enclosing class if the type is an inner class.
    This function also calls setDestructorFunction().

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a enclosingObject
            \li The module or enclosing class to which the new type will be added.
        \row
            \li \a typeName
            \li Name by which the type will be known in Python.
        \row
            \li \a originalName
            \li Original C++ name of the type.
        \row
            \li \a typeSpec
            \li The new type to be initialized and added to the module.
        \row
            \li \a cppObjDtor
            \li Memory deallocation function for the C++ object held by the type.
                Should not be used if the underlying C++ class has a private
                destructor.
        \row
            \li \a bases
            \li Base types from which the new type inherits.
        \row
            \li \a wrapperFlags
            \li Flags, see WrapperFlags.
    \endtable

    Returns the new type or \nullptr if the initialization failed.
 */
PyTypeObject *
introduceWrapperType(PyObject *enclosingObject,
                     const char *typeName,
                     const char *originalName,
                     PyType_Spec *typeSpec,
                     ObjectDestructor cppObjDtor,
                     PyObject *bases,
                     unsigned wrapperFlags)
{
    assert(PySequence_Size(bases) > 0);
    typeSpec->slots[0].pfunc = PySequence_GetItem(bases, 0);

    auto *type = SbkType_FromSpecBasesMeta(typeSpec, bases, SbkObjectType_TypeF());

    auto *sotp = PepType_SOTP(type);
    if (wrapperFlags & DeleteInMainThread)
        sotp->delete_in_main_thread = 1;
    sotp->type_behaviour = (wrapperFlags & Value) != 0
                           ? BEHAVIOUR_VALUETYPE : BEHAVIOUR_OBJECTTYPE;

    setOriginalName(type, originalName);
    setDestructorFunction(type, cppObjDtor);
    auto *ob_type = reinterpret_cast<PyObject *>(type);

    if (wrapperFlags & InternalWrapper) {
        // Type wraps another wrapper class and isn't part of any module.  This is used to extend
        // Qt types with additional functionality, but from within PySide itself.  In order for
        // the re-wrapped class to call the autogenerated __init__ method, is_user_type must be set
        // to 1.  Otherwise the Shiboken::ObjectType::canCallConstructor test will fail.
        // This is currently only used by QtRemoteObjects to create types dynamically.
        sotp->is_user_type = 1;
        return type;
    }

    if (wrapperFlags & InnerClass) {
        // PYSIDE-2230: Instead of tp_dict, use the enclosing type.
        //              This stays interface compatible.
        if (PyType_Check(enclosingObject)) {
            AutoDecRef tpDict(PepType_GetDict(reinterpret_cast<PyTypeObject *>(enclosingObject)));
            return PyDict_SetItemString(tpDict, typeName, ob_type) == 0 ? type : nullptr;
        }
        if (PyDict_Check(enclosingObject))
            return PyDict_SetItemString(enclosingObject, typeName, ob_type) == 0 ? type : nullptr;
    }

    // PyModule_AddObject steals type's reference.
    Py_INCREF(ob_type);
    if (PepModule_AddType(enclosingObject, type) != 0) {
        std::cerr << "Warning: " << __FUNCTION__ << " returns nullptr for "
            << typeName << '/' << originalName << " due to PyModule_AddObject(enclosingObject="
            << enclosingObject << ", ob_type=" << ob_type << ") failing\n";
        return nullptr;
    }
    return type;
}

void setSubTypeInitHook(PyTypeObject *type, SubTypeInitHook func)
{
    assert(SbkObjectType_Check(type));
    PepType_SOTP(type)->subtype_init = func;
}

void *getTypeUserData(PyTypeObject *type)
{
    assert(SbkObjectType_Check(type));
    return PepType_SOTP(type)->user_data;
}

void setTypeUserData(PyTypeObject *type, void *userData, DeleteUserDataFunc d_func)
{
    assert(SbkObjectType_Check(type));
    auto *sotp = PepType_SOTP(type);
    sotp->user_data = userData;
    sotp->d_func = d_func;
}

// Try to find the exact type of cptr.
PyTypeObject *typeForTypeName(const char *typeName)
{
    PyTypeObject *result{};
    if (typeName) {
        if (PyTypeObject *pyType = Shiboken::Conversions::getPythonTypeObject(typeName))
            result = pyType;
    }
    return result;
}

bool hasSpecialCastFunction(PyTypeObject *sbkType)
{
    const auto *d = PepType_SOTP(sbkType);
    return d != nullptr && d->mi_specialcast != nullptr;
}

// Find whether base is a direct single line base class of type
// (no multiple inheritance), that is, a C++ pointer cast can safely be done.
static bool isDirectAncestor(PyTypeObject *type, PyTypeObject *base)
{
    if (type == base)
        return true;
    if (PyTuple_Size(type->tp_bases) == 0)
        return false;
    auto *sbkObjectType = SbkObject_TypeF();
    auto *firstBase = reinterpret_cast<PyTypeObject *>(PyTuple_GetItem(type->tp_bases, 0));
    return firstBase != sbkObjectType
           && PyType_IsSubtype(type, sbkObjectType) != 0
           && isDirectAncestor(firstBase, base);
}

bool canDowncastTo(PyTypeObject *baseType, PyTypeObject *targetType)
{
    return isDirectAncestor(targetType, baseType);
}

} // namespace ObjectType


/*!
    \namespace Shiboken::Object

    \brief Namespace for object instance related things.

    \inmodule libshiboken
*/
namespace Object
{

static void recursive_invalidate(SbkObject *self, std::set<SbkObject *> &seen);

bool checkType(PyObject *pyObj)
{
    return ObjectType::checkType(Py_TYPE(pyObj));
}

bool isUserType(PyObject *pyObj)
{
    return ObjectType::isUserType(Py_TYPE(pyObj));
}

Py_hash_t hash(PyObject *pyObj)
{
    assert(Shiboken::Object::checkType(pyObj));
    return reinterpret_cast<Py_hash_t>(pyObj);
}

static void setSequenceOwnership(PyObject *pyObj, bool owner)
{
    using OwnershipFunc = void (*)(SbkObject *);

    traverseSequence(pyObj, owner ? OwnershipFunc(getOwnership) : OwnershipFunc(releaseOwnership));
}

void setValidCpp(SbkObject *pyObj, bool value)
{
    pyObj->d->validCppObject = value;
}

void setHasCppWrapper(SbkObject *pyObj, bool value)
{
    pyObj->d->containsCppWrapper = value;
}

bool hasCppWrapper(SbkObject *pyObj)
{
    return pyObj->d->containsCppWrapper;
}

bool wasCreatedByPython(SbkObject *pyObj)
{
    return pyObj->d->cppObjectCreated;
}

void callCppDestructors(SbkObject *pyObj)
{
    auto *priv = pyObj->d;
    if (priv->isQAppSingleton && DestroyQApplication) {
        // PYSIDE-1470: Allow to destroy the application from Shiboken.
        DestroyQApplication();
        return;
    }
    auto *sotp = PepType_SOTP(Shiboken::pyType(pyObj));
    if (sotp->is_multicpp) {
        callDestructor(getDestructorEntries(pyObj));
    } else {
        Shiboken::ThreadStateSaver threadSaver;
        threadSaver.save();
        sotp->cpp_dtor(Shiboken::cppPointer(priv));
    }

    if (priv->validCppObject && priv->containsCppWrapper) {
        BindingManager::instance().releaseWrapper(pyObj);
    }

    /* invalidate needs to be called before deleting pointer array because
       it needs to delete entries for them from the BindingManager hash table;
       also release wrapper explicitly if object contains C++ wrapper because
       invalidate doesn't */
    invalidate(pyObj);

    clearCppPointers(pyObj);
    priv->validCppObject = false;
}

bool hasOwnership(SbkObject *pyObj)
{
    return pyObj->d->hasOwnership;
}

static SbkObject *getSbkParent(SbkObject *sbkObj)
{
    if (sbkObj != nullptr && sbkObj->d != nullptr && sbkObj->d->parentInfo != nullptr)
        return sbkObj->d->parentInfo->parent;
    return nullptr;
}

void getOwnership(SbkObject *sbkObj)
{
    // skip if already have the ownership
    if (sbkObj->d->hasOwnership)
        return;

    // skip if this object has parent
    if (getSbkParent(sbkObj) != nullptr)
        return;

    // Get back the ownership
    sbkObj->d->hasOwnership = true;

    if (sbkObj->d->containsCppWrapper)
        Py_DECREF(reinterpret_cast<PyObject *>(sbkObj)); // Remove extra ref
    else
        makeValid(sbkObj); // Make the object valid again
}

void getOwnership(PyObject *pyObj)
{
    if (pyObj)
        setSequenceOwnership(pyObj, true);
}

void releaseOwnership(SbkObject *sbkObj)
{
    // skip if the ownership have already moved to c++
    auto *ob  = reinterpret_cast<PyObject *>(sbkObj);
    auto *selfType = Py_TYPE(ob);
    if (!sbkObj->d->hasOwnership || Shiboken::Conversions::pythonTypeIsValueType(PepType_SOTP(selfType)->converter))
        return;

    // remove object ownership
    sbkObj->d->hasOwnership = false;

    // If We have control over object life
    if (sbkObj->d->containsCppWrapper)
        Py_INCREF(ob); // keep the python object alive until the wrapper destructor call
    else
        invalidate(sbkObj); // If I do not know when this object will die We need to invalidate this to avoid use after
}

void releaseOwnership(PyObject *pyObj)
{
    setSequenceOwnership(pyObj, false);
}

/* Needed forward declarations */
static void recursive_invalidate(PyObject *pyobj, std::set<SbkObject *> &seen);

void invalidate(PyObject *pyobj)
{
    std::set<SbkObject *> seen;
    recursive_invalidate(pyobj, seen);
}

void invalidate(SbkObject *self)
{
    std::set<SbkObject *> seen;
    recursive_invalidate(self, seen);
}

static void recursive_invalidate(PyObject *pyobj, std::set<SbkObject *> &seen)
{
    auto func = [&seen](SbkObject *o) { recursive_invalidate(o, seen); };
    traverseSequence(pyobj, func);
}

static void recursive_invalidate(SbkObject *self, std::set<SbkObject *> &seen)
{
    // Skip if this object not is a valid object or if it's already been seen
    if (!self || reinterpret_cast<PyObject *>(self) == Py_None || seen.find(self) != seen.end())
        return;
    seen.insert(self);

    if (!self->d->containsCppWrapper) {
        self->d->validCppObject = false; // Mark object as invalid only if this is not a wrapper class
        BindingManager::instance().releaseWrapper(self);
    }

    // If it is a parent invalidate all children.
    if (self->d->parentInfo) {
        // Create a copy because this list can be changed during the process
        ChildrenList copy = self->d->parentInfo->children;

        for (SbkObject *child : copy) {
            // invalidate the child
            recursive_invalidate(child, seen);

            // if the parent not is a wrapper class, then remove children from him, because We do not know when this object will be destroyed
            if (!self->d->validCppObject)
                removeParent(child, true, true);
        }
    }

    // If has ref to other objects invalidate all
    if (auto *rInfo = self->d->referredObjects) {
        for (const auto &p : *rInfo)
            recursive_invalidate(p.second, seen);
    }
}

void makeValid(SbkObject *self)
{
    // Skip if this object not is a valid object
    if (!self || reinterpret_cast<PyObject *>(self) == Py_None || self->d->validCppObject)
        return;

    // Mark object as invalid only if this is not a wrapper class
    self->d->validCppObject = true;

    // If it is a parent make  all children valid
    if (self->d->parentInfo) {
        for (SbkObject *child : self->d->parentInfo->children)
            makeValid(child);
    }

    // If has ref to other objects make all valid again
    if (self->d->referredObjects) {
        const RefCountMap &refCountMap = *(self->d->referredObjects);
        for (const auto &p : refCountMap) {
            if (Shiboken::Object::checkType(p.second))
                makeValid(reinterpret_cast<SbkObject *>(p.second));
        }
    }
}

void *cppPointer(SbkObject *pyObj)
{
    if (pyObj == nullptr || pyObj->d == nullptr)
        return nullptr;
    return Shiboken::cppPointer(pyObj->d);
}

void *cppPointer(SbkObject *pyObj, PyTypeObject *desiredType)
{
    if (pyObj == nullptr || pyObj->d == nullptr)
        return nullptr;
    if (pyObj->d->is_multicpp == 0)
        return static_cast<SbkObjectPrivate1 *>(pyObj->d)->cptr;
    auto *mid = static_cast<SbkObjectMultiInheritancePrivate *>(pyObj->d);
    const unsigned idx = getTypeIndexOnHierarchy(Shiboken::pyType(pyObj), desiredType);
    if (idx >= mid->cptrs.size()) {
        setBaseClassIndexError(Shiboken::pyType(pyObj), desiredType);
        return nullptr;
    }
    return mid->cptrs[idx];
}

std::vector<void *> cppPointers(SbkObject *pyObj)
{
    if (pyObj == nullptr || pyObj->d == nullptr)
        return {};
    return pyObj->d->is_multicpp != 0
               ? static_cast<SbkObjectMultiInheritancePrivate *>(pyObj->d)->cptrs
               : std::vector<void *>(1, static_cast<SbkObjectPrivate1 *>(pyObj->d)->cptr);
}

bool setCppPointer(SbkObject *sbkObj, PyTypeObject *desiredType, void *cptr)
{
    PyTypeObject *type = Shiboken::pyType(sbkObj);

    if (sbkObj->d->is_multicpp == 0) {
        auto *d = static_cast<SbkObjectPrivate1 *>(sbkObj->d);
        if (d->cptr != nullptr) {
            setAlreadyInitializedError(type, desiredType);
            return false;
        }
        d->cptr = cptr;
    } else {
        const unsigned idx = getTypeIndexOnHierarchy(type, desiredType);
        auto *mid = static_cast<SbkObjectMultiInheritancePrivate *>(sbkObj->d);
        if (idx >= mid->cptrs.size()) {
            setBaseClassIndexError(type, desiredType);
            return false;
        }
        if (mid->cptrs[idx] != nullptr) {
            setAlreadyInitializedError(type, desiredType);
            return false;
        }
        mid->cptrs[idx] = cptr;
    }

    sbkObj->d->cppObjectCreated = true;
    return true;
}

bool isValid(PyObject *pyObj)
{
    if (pyObj == nullptr || pyObj == Py_None || PyType_Check(pyObj) != 0)
        return true;

    PyTypeObject *type = Py_TYPE(pyObj);
    if (Py_TYPE(reinterpret_cast<PyObject *>(type)) != SbkObjectType_TypeF())
        return true;

    auto *priv = reinterpret_cast<SbkObject *>(pyObj)->d;

    if (!priv->cppObjectCreated && isUserType(pyObj)) {
        PyErr_Format(PyExc_RuntimeError,
                     "libshiboken: '__init__' method of object's base class (%s) not called.",
                     Py_TYPE(pyObj)->tp_name);
        return false;
    }

    if (!priv->validCppObject) {
        PyErr_Format(PyExc_RuntimeError,
                     "libshiboken: Internal C++ object (%s) already deleted.",
                     Py_TYPE(pyObj)->tp_name);
        return false;
    }

    return true;
}

bool isValid(SbkObject *pyObj, bool throwPyError)
{
    if (!pyObj)
        return false;

    SbkObjectPrivate *priv = pyObj->d;
    auto *ob = reinterpret_cast<PyObject *>(pyObj);
    if (!priv->cppObjectCreated && isUserType(ob)) {
        if (throwPyError)
            PyErr_Format(PyExc_RuntimeError,
                         "libshiboken: Base constructor of the object (%s) not called.",
                         Py_TYPE(ob)->tp_name);
        return false;
    }

    if (!priv->validCppObject) {
        if (throwPyError)
            PyErr_Format(PyExc_RuntimeError,
                         "libshiboken: Internal C++ object (%s) already deleted.",
                         (Py_TYPE(ob))->tp_name);
        return false;
    }

    return true;
}

bool isValid(PyObject *pyObj, bool throwPyError)
{
    if (!pyObj || pyObj == Py_None ||
        !PyType_IsSubtype(Py_TYPE(pyObj), SbkObject_TypeF())) {
        return true;
    }
    return isValid(reinterpret_cast<SbkObject *>(pyObj), throwPyError);
}

SbkObject *findColocatedChild(SbkObject *wrapper,
                              const PyTypeObject *instanceType)
{
    // Degenerate case, wrapper is the correct wrapper.
    if (reinterpret_cast<const void *>(Shiboken::pyType(wrapper)) == reinterpret_cast<const void *>(instanceType))
        return wrapper;

    void *wrapperCptr = Shiboken::Object::cppPointer(wrapper);
    if (wrapperCptr == nullptr)
        return nullptr;

    ParentInfo *pInfo = wrapper->d->parentInfo;
    if (!pInfo)
        return nullptr;

    ChildrenList &children = pInfo->children;

    for (SbkObject *child : children) {
        void *childCPtr = Shiboken::Object::cppPointer(child);
        if (childCPtr != nullptr && childCPtr == wrapperCptr) {
            auto *childType = Shiboken::pyType(child);
            return reinterpret_cast<const void *>(childType) == reinterpret_cast<const void *>(instanceType)
                ? child : findColocatedChild(child, instanceType);
        }
    }
    return nullptr;
}

/*!
    Bind a C++ object to Python. Forwards to
    newObjectWithHeuristics(), newObjectForType() depending on \a isExactType.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a instanceType
            \li equivalent Python type for the C++ object.
        \row
            \li \a cptr
            \li C++ instance.
        \row
            \li \a hasOwnership
            \li If true, Python will try to delete the underlying C++ object
                when there are no more references.
        \row
            \li \a isExactType
            \li If false, Shiboken will use some heuristics to detect the
                correct Python type of this C++ object. In any case,
                \a instanceType must be provided; it'll be used as search
                starting point and as fallback.
        \row
            \li \a typeName
            \li If non-null, this will be used as helper to find the correct
                Python type for this object.
    \endtable
*/
PyObject *newObject(PyTypeObject *instanceType,
                    void *cptr,
                    bool hasOwnership,
                    bool isExactType,
                    const char *typeName)
{
    return isExactType
        ? newObjectForType(instanceType, cptr, hasOwnership)
        : newObjectWithHeuristics(instanceType, cptr, hasOwnership, typeName);
}

static PyObject *newObjectWithHeuristicsHelper(PyTypeObject *instanceType,
                                               const char *cppTypeIdName,
                                               void *cptr,
                                               bool hasOwnership)
{
    // Try to find the exact type of cptr via the name obtained from "typeId(ptr).name()"
    // as registered with the shiboken converters.
    PyTypeObject *exactType = ObjectType::typeForTypeName(cppTypeIdName);

    // Nothing found or same name returned (in case of a hierarchy with non-virtual destructors
    // like QStyleOption). Try type discovery in these cases.
    if (exactType == nullptr || exactType == instanceType) {
        if (auto resolved = BindingManager::instance().findDerivedType(cptr, instanceType);
            resolved.first != nullptr) {
            return newObjectForType(resolved.first, resolved.second, hasOwnership);
        }
    } else {
        // Direct, single line inheritance: Use exactType
        if (Shiboken::ObjectType::canDowncastTo(instanceType, exactType))
            return newObjectForType(exactType, cptr, hasOwnership);
        // Multiple inheritance: Try to run type discovery on the type found to cast cptr
        if (auto *derivedCptr = BindingManager::runTypeDiscovery(cptr, exactType, instanceType))
            return newObjectForType(exactType, derivedCptr, hasOwnership);
    }

    // Fall back to base type
    return newObjectForType(instanceType, cptr, hasOwnership);
}

/*!
    Bind a C++ object to Python for polymorphic pointers. Calls
    newObjectWithHeuristics() with an additional check for multiple
    inheritance, in which case it will fall back to instanceType.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a instanceType
            \li Equivalent Python type for the C++ object.
        \row
            \li \a cptr
            \li C++ instance.
        \row
            \li \a hasOwnership
            \li If true, Python will try to delete the underlying C++ object
                when there are no more references.
        \row
            \li \a typeName
            \li If non-null, this will be used as helper to find the correct
                Python type for this object (obtained by typeid().name()).
    \endtable
*/
PyObject *newObjectForPointer(PyTypeObject *instanceType,
                              void *cptr,
                              bool hasOwnership,
                              const char *typeName)
{
    return newObjectWithHeuristicsHelper(instanceType, typeName, cptr, hasOwnership);
}

/*!
    Bind a C++ object to Python using some heuristics to detect the correct
    Python type of this C++ object. In any case \a instanceType must be provided;
    it'll be used as search starting point and as fallback.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a instanceType
            \li Equivalent Python type for the C++ object.
        \row
            \li \a cptr
            \li C++ instance.
        \row
            \li \a hasOwnership
            \li If true, Python will try to delete the underlying C++ object
                when there are no more references.
        \row
            \li \a typeName
            \li If non-null, this will be used as helper to find the correct
                Python type for this object (obtained by typeid().name()).
    \endtable
*/
PyObject *newObjectWithHeuristics(PyTypeObject *instanceType,
                                  void *cptr,
                                  bool hasOwnership,
                                  const char *typeName)
{
    return newObjectWithHeuristicsHelper(instanceType, typeName,
                                         cptr, hasOwnership);
}

/*!
    Bind a C++ object to Python using the given type.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a instanceType
            \li Equivalent Python type for the C++ object.
        \row
            \li \a cptr
            \li C++ instance.
        \row
            \li \a hasOwnership
            \li If true, Python will try to delete the underlying
                C++ object when there are no more references.
    \endtable
*/
PyObject *newObjectForType(PyTypeObject *instanceType, void *cptr, bool hasOwnership)
{
    auto &bindingManager = BindingManager::instance();
    SbkObject *self = bindingManager.retrieveWrapper(cptr, instanceType);
    if (self != nullptr) {
        Py_IncRef(reinterpret_cast<PyObject *>(self));
    } else {
        self = reinterpret_cast<SbkObject *>(SbkObject_tp_new(instanceType, nullptr, nullptr));
        Shiboken::setCppPointer(self, cptr);
        self->d->hasOwnership = hasOwnership;
        self->d->validCppObject = 1;
        bindingManager.registerWrapper(self, cptr);
    }
    return reinterpret_cast<PyObject *>(self);
}

void destroy(SbkObject *self, void *cppData)
{
    // Skip if this is called with NULL pointer this can happen in derived classes
    if (!self)
        return;

    // This can be called in c++ side
    Shiboken::GilState gil;

    // Remove all references attached to this object
    clearReferences(self);

    // Remove the object from parent control

    // Verify if this object has parent
    const bool hasParent = getSbkParent(self) != nullptr;

    if (self->d->parentInfo) {
        // Check for children information and make all invalid if they exists
        _destroyParentInfo(self, true);
        // If this object has parent then the pyobject can be invalid now, because we remove the last ref after remove from parent
    }

    //if !hasParent this object could still alive
    if (!hasParent && self->d->containsCppWrapper && !self->d->hasOwnership) {
        // Remove extra ref used by c++ object this will case the pyobject destruction
        // This can cause the object death
        Py_DECREF(reinterpret_cast<PyObject *>(self));
    }

    //Python Object is not destroyed yet
    if (cppData && Shiboken::BindingManager::instance().hasWrapper(cppData)) {
        // Remove from BindingManager
        Shiboken::BindingManager::instance().releaseWrapper(self);
        self->d->hasOwnership = false;

        // the cpp object instance was deleted
        clearCppPointers(self);
    }

    // After this point the object can be death do not use the self pointer bellow
}

void removeParent(SbkObject *child, bool giveOwnershipBack, bool keepReference)
{
    ParentInfo *pInfo = child->d->parentInfo;
    if (!pInfo || !pInfo->parent) {
        if (pInfo && pInfo->hasWrapperRef) {
            pInfo->hasWrapperRef = false;
        }
        return;
    }

    ChildrenList &oldBrothers = pInfo->parent->d->parentInfo->children;
    // Verify if this child is part of parent list
    auto iChild = oldBrothers.find(child);
    if (iChild == oldBrothers.end())
        return;

    oldBrothers.erase(iChild);

    pInfo->parent = nullptr;

    // This will keep the wrapper reference, will wait for wrapper destruction to remove that
    if (keepReference &&
        child->d->containsCppWrapper) {
        //If have already a extra ref remove this one
        if (pInfo->hasWrapperRef)
            Py_DECREF(child);
        else
            pInfo->hasWrapperRef = true;
        return;
    }

    // Transfer ownership back to Python
    child->d->hasOwnership = giveOwnershipBack;

    // Remove parent ref
    Py_DECREF(child);
}

void setParent(PyObject *parent, PyObject *child)
{
    if (!child || child == Py_None || child == parent)
        return;

    /*
     * setParent is recursive when the child is a native Python sequence, i.e. objects not binded by Shiboken
     * like tuple and list.
     *
     * This "limitation" exists to fix the following problem: A class multiple inherits QObject and QString,
     * so if you pass this class to someone that takes the ownership, we CAN'T enter in this if, but hey! QString
     * follows the sequence protocol.
     */
    if (PySequence_Check(child) && !Object::checkType(child)) {
        Shiboken::AutoDecRef seq(PySequence_Fast(child, nullptr));
        for (Py_ssize_t i = 0, max = PySequence_Size(seq); i < max; ++i) {
            Shiboken::AutoDecRef obj(PySequence_GetItem(seq.object(), i));
            setParent(parent, obj);
        }
        return;
    }

    bool parentIsNull = !parent || parent == Py_None;
    auto *parent_ = reinterpret_cast<SbkObject *>(parent);
    auto *child_ = reinterpret_cast<SbkObject *>(child);

    if (!parentIsNull) {
        if (!parent_->d->parentInfo)
            parent_->d->parentInfo = new ParentInfo;

        // do not re-add a child
        if (getSbkParent(child_) == parent_) {
            if (Shiboken::pyVerbose()) {
                std::cerr << "Warning: Attempt to re-add child "
                          << child << '/' << Py_TYPE(child)->tp_name << " to parent "
                          << parent << '/' << Py_TYPE(parent)->tp_name << '\n';
            }
            return;
        }
    }

    ParentInfo *pInfo = child_->d->parentInfo;
    bool hasAnotherParent = pInfo && pInfo->parent && pInfo->parent != parent_;

    //Avoid destroy child during reparent operation
    Py_INCREF(child);

    // check if we need to remove this child from the old parent
    if (parentIsNull || hasAnotherParent)
        removeParent(child_);

    // Add the child to the new parent
    pInfo = child_->d->parentInfo;
    if (!parentIsNull) {
        if (!pInfo)
            pInfo = child_->d->parentInfo = new ParentInfo;

        pInfo->parent = parent_;
        parent_->d->parentInfo->children.insert(child_);

        // Add Parent ref
        Py_INCREF(child);

        // Remove ownership
        child_->d->hasOwnership = false;
    }

    // Remove previous safe ref
    Py_DECREF(child);
}

void setParentReturnValueHeuristics(PyObject *parent, PyObject *child)
{
    // Do not set a parent if the child already has one. This reduces damage
    // in case of inadvertent return value heuristics.
    if (child != nullptr && child != Py_None && Object::checkType(child)
        && getSbkParent(reinterpret_cast<SbkObject *>(child)) == nullptr) {
        setParent(parent, child);
    }
}

void deallocData(SbkObject *self, bool cleanup)
{
    // Make cleanup if this is not a wrapper otherwise this will be done on wrapper destructor
    if(cleanup) {
        removeParent(self);

        if (self->d->parentInfo)
            _destroyParentInfo(self, true);

        clearReferences(self);
    }

    // Remove from BindingManager
    Shiboken::BindingManager::instance().releaseWrapper(self);
    clearCppPointers(self);
    // delete self->d; PYSIDE-205: wrong!

    // PYSIDE-205: always delete d.
    if (self->d->is_multicpp)
        delete static_cast<SbkObjectMultiInheritancePrivate *>(self->d);
    else
        delete static_cast<SbkObjectPrivate1 *>(self->d);
    Py_XDECREF(self->ob_dict);
    PepExt_TypeCallFree(reinterpret_cast<PyObject *>(self));
}

void setTypeUserData(SbkObject *wrapper, void *userData, DeleteUserDataFunc d_func)
{
    auto *type = Shiboken::pyType(wrapper);
    auto *sotp = PepType_SOTP(type);
    if (sotp->user_data)
        sotp->d_func(sotp->user_data);

    sotp->d_func = d_func;
    sotp->user_data = userData;
}

void *getTypeUserData(SbkObject *wrapper)
{
    auto *type = Shiboken::pyType(wrapper);
    return PepType_SOTP(type)->user_data;
}

static inline bool isNone(const PyObject *o)
{
    return o == nullptr || o == Py_None;
}

static void removeRefCountKey(SbkObject *self, const std::string &key)
{
    if (self->d->referredObjects) {
        const auto iterPair = self->d->referredObjects->equal_range(key);
        if (iterPair.first != iterPair.second) {
            decRefPyObjectList(iterPair.first, iterPair.second);
            self->d->referredObjects->erase(iterPair.first, iterPair.second);
        }
    }
}

/*!
    Increments the reference count of the referred Python object.
    A previous Python object in the same position identified by \a keyC
    will have its reference counter decremented automatically when replaced
    unless \a append is true.
    All the kept references should be decremented when the Python wrapper
    indicated by \a self dies.
    No checking is done for any of the passed arguments, since it is meant to
    be used by generated code it is supposed that the generator is correct.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a self
            \li The wrapper instance that keeps references to other objects.
        \row
            \li \a keyC
            \li A key that identifies the C++ method signature and argument
                where the referred object came from.
        \row
            \li \a referredObject
            \li The object whose reference is used by the \a self object.
        \row
            \li \a append
            \li If true, \a referredObject is appended to the list of
                references kept under \a keyC instead of replacing them.
    \endtable
*/
void keepReference(SbkObject *self, const char *keyC, PyObject *referredObject, bool append)
{
    std::string key(keyC);

    if (isNone(referredObject)) {
        removeRefCountKey(self, key);
        return;
    }

    if (!self->d->referredObjects) {
        self->d->referredObjects =
            new Shiboken::RefCountMap{RefCountMap::value_type{key, referredObject}};
        Py_INCREF(referredObject);
        return;
    }

    RefCountMap &refCountMap = *(self->d->referredObjects);
    const auto iterPair = refCountMap.equal_range(key);
    if (std::any_of(iterPair.first, iterPair.second,
                    [referredObject](const RefCountMap::value_type &v) { return v.second == referredObject; })) {
        return;
    }

    if (!append && iterPair.first != iterPair.second) {
        decRefPyObjectList(iterPair.first, iterPair.second);
        refCountMap.erase(iterPair.first, iterPair.second);
    }

    refCountMap.insert(RefCountMap::value_type{key, referredObject});
    Py_INCREF(referredObject);
}

/*!
    Removes any reference previously added by keepReference().

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a self
            \li The wrapper instance that keeps references to other objects.
        \row
            \li \a key
            \li A key that identifies the C++ method signature and argument
                where the referred object came from.
        \row
            \li \a referredObject
            \li The object whose reference is used by the \a self object.
    \endtable
*/
void removeReference(SbkObject *self, const char *key, PyObject *referredObject)
{
    if (!isNone(referredObject))
        removeRefCountKey(self, std::string(key));
}

void clearReferences(SbkObject *self)
{
    if (!self->d->referredObjects)
        return;

    RefCountMap &refCountMap = *(self->d->referredObjects);
    for (auto it = refCountMap.begin(), end = refCountMap.end(); it != end; ++it)
        Py_DECREF(it->second);
    self->d->referredObjects->clear();
}

SbkConverter *getConverter(PyTypeObject *type)
{
    return PepType_SOTP(type)->converter;
}

} // namespace Object

} // namespace Shiboken
