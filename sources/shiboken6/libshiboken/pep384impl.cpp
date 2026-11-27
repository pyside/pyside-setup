// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#define PEP384_INTERN

#include "pep384impl.h"
#include "pep384impl_p.h"
#include "autodecref.h"
#include "sbkpytuplerange.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"
#include "basewrapper.h"
#include "basewrapper_p.h"
#include "sbkenum_p.h"
#include "voidptr.h"
#ifdef Py_GIL_DISABLED
#  include "sbkftoptions.h"
#  include "sbkheldlocks.h"
#  include "shibokenclasshelpermacros.h"
#  include <mutex>
#  include <optional>
#endif

#include <cstdlib>
#include <cstring>

extern "C"
{

/*
 * The documentation is located in `sources/pyside6/doc/developer/limited_api.rst`.

 * Here is the verification code for PyTypeObject.
 * We create a type object and check if its fields
 * appear at the right offsets.
 */
#ifdef Py_LIMITED_API

#define make_dummy_int(x)   (x * sizeof(void *))
#define make_dummy(x)       (reinterpret_cast<void *>(make_dummy_int(x)))

static PyObject *
dummy_func(PyObject * /* self */, PyObject * /* args */)
{
    Py_RETURN_NONE;
}

static struct PyMethodDef probe_methoddef[] = {
    {"dummy", dummy_func, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}
};

static PyGetSetDef probe_getseters[] = {
    {nullptr, nullptr, nullptr, nullptr, nullptr}  /* Sentinel */
};

static PyMemberDef probe_members[] = {
    {nullptr, 0, 0, 0, nullptr}  /* Sentinel */
};

#define probe_tp_dealloc    make_dummy(1)
#define probe_tp_repr       make_dummy(2)
#define probe_tp_call       make_dummy(3)
#define probe_tp_getattro   make_dummy(16)
#define probe_tp_setattro   make_dummy(17)
#define probe_tp_str        make_dummy(4)
#define probe_tp_traverse   make_dummy(5)
#define probe_tp_clear      make_dummy(6)
#define probe_tp_iternext   make_dummy(7)
#define probe_tp_methods    probe_methoddef
#define probe_tp_members    probe_members
#define probe_tp_getset     probe_getseters
#define probe_tp_descr_get  make_dummy(10)
#define probe_tp_descr_set  make_dummy(18)
#define probe_tp_init       make_dummy(11)
#define probe_tp_alloc      make_dummy(12)
#define probe_tp_new        make_dummy(13)
#define probe_tp_free       make_dummy(14)
#define probe_tp_is_gc      make_dummy(15)

#define probe_tp_name       "type.probe"
#define probe_tp_basicsize  make_dummy_int(42)

static PyType_Slot typeprobe_slots[] = {
    {Py_tp_dealloc,     probe_tp_dealloc},
    {Py_tp_repr,        probe_tp_repr},
    {Py_tp_call,        probe_tp_call},
    {Py_tp_getattro,    probe_tp_getattro},
    {Py_tp_setattro,    probe_tp_setattro},
    {Py_tp_str,         probe_tp_str},
    {Py_tp_traverse,    probe_tp_traverse},
    {Py_tp_clear,       probe_tp_clear},
    {Py_tp_iternext,    probe_tp_iternext},
    {Py_tp_methods,     probe_tp_methods},
    {Py_tp_members,     probe_tp_members},
    {Py_tp_getset,      probe_tp_getset},
    {Py_tp_descr_get,   probe_tp_descr_get},
    {Py_tp_descr_set,   probe_tp_descr_set},
    {Py_tp_init,        probe_tp_init},
    {Py_tp_alloc,       probe_tp_alloc},
    {Py_tp_new,         probe_tp_new},
    {Py_tp_free,        probe_tp_free},
    {Py_tp_is_gc,       probe_tp_is_gc},
    {0, nullptr}
};
static PyType_Spec typeprobe_spec = {
    probe_tp_name,
    probe_tp_basicsize,
    0,
    Py_TPFLAGS_DEFAULT,
    typeprobe_slots,
};

static void
check_PyTypeObject_valid()
{
    auto *typetype = &PyType_Type;
    auto *obtype = reinterpret_cast<PyObject *>(typetype);
    auto *probe_tp_base_obj = PyObject_GetAttr(obtype, Shiboken::PyMagicName::base());
    auto *probe_tp_base = reinterpret_cast<PyTypeObject *>(probe_tp_base_obj);
    auto *probe_tp_bases = PyObject_GetAttr(obtype, Shiboken::PyMagicName::bases());
    auto *checkObj = PyType_FromSpecWithBases(&typeprobe_spec, probe_tp_bases);
    auto *check = reinterpret_cast<PyTypeObject *>(checkObj);
    PyObject *w = PyObject_GetAttr(obtype, Shiboken::PyMagicName::weakrefoffset());
    long probe_tp_weakrefoffset = PyLong_AsLong(w);
    PyObject *d = PyObject_GetAttr(obtype, Shiboken::PyMagicName::dictoffset());
    long probe_tp_dictoffset = PyLong_AsLong(d);
    PyObject *probe_tp_mro = PyObject_GetAttr(obtype, Shiboken::PyMagicName::mro());
    Shiboken::AutoDecRef tpDict(PepType_GetDict(check));
    auto *checkDict = tpDict.object();
    if (false
        || std::strcmp(probe_tp_name, check->tp_name) != 0
        || probe_tp_basicsize       != check->tp_basicsize
        || probe_tp_dealloc         != check->tp_dealloc
        || probe_tp_repr            != check->tp_repr
        || probe_tp_call            != check->tp_call
        || probe_tp_getattro        != check->tp_getattro
        || probe_tp_setattro        != check->tp_setattro
        || probe_tp_str             != check->tp_str
        || probe_tp_traverse        != check->tp_traverse
        || probe_tp_clear           != check->tp_clear
        || probe_tp_weakrefoffset   != typetype->tp_weaklistoffset
        || probe_tp_iternext        != check->tp_iternext
        || probe_tp_methods         != check->tp_methods
        || probe_tp_getset          != check->tp_getset
        || probe_tp_base            != typetype->tp_base
        || !PyDict_Check(checkDict)
        || !PyDict_GetItemString(checkDict, "dummy")
        || probe_tp_descr_get       != check->tp_descr_get
        || probe_tp_descr_set       != check->tp_descr_set
        || probe_tp_dictoffset      != typetype->tp_dictoffset
        || probe_tp_init            != check->tp_init
        || probe_tp_alloc           != check->tp_alloc
        || probe_tp_new             != check->tp_new
        || probe_tp_free            != check->tp_free
        || probe_tp_is_gc           != check->tp_is_gc
        || probe_tp_bases           != typetype->tp_bases
        || probe_tp_mro             != typetype->tp_mro
        || Py_TPFLAGS_DEFAULT       != (check->tp_flags & Py_TPFLAGS_DEFAULT))
        Py_FatalError("libshiboken: The structure of type objects has changed!");
    Py_DECREF(checkObj);
    Py_DECREF(probe_tp_base_obj);
    Py_DECREF(w);
    Py_DECREF(d);
    Py_DECREF(probe_tp_bases);
    Py_DECREF(probe_tp_mro);
}

#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Additional for object.h / class properties
 *
 */
#ifdef Py_LIMITED_API
/*
 * This implementation of `_PyType_Lookup` works for lookup in our classes.
 * The implementation ignores all caching and versioning and is also
 * less optimized. This is reduced from the Python implementation.
 */

/* Internal API to look for a name through the MRO, bypassing the method cache.
   This returns a borrowed reference, and might set an exception.
   'error' is set to: -1: error with exception; 1: error without exception; 0: ok */
static PyObject *
find_name_in_mro(PyTypeObject *type, PyObject *name, int *error)
{
    *error = 0;
    /* Look in tp_dict of types in MRO. Keep a strong reference to mro because
     * type->tp_mro can be replaced during dict lookup, e.g. when comparing to
     * non-string keys. */
    assert(PyTuple_Check(type->tp_mro));
    for (PyTypeObject *base : Shiboken::OwningPyTupleTypeRange(type->tp_mro)) {
        Shiboken::AutoDecRef dict(PepType_GetDict(base));
        assert(!dict.isNull() && PyDict_Check(dict.object()));
        if (auto *res = PyDict_GetItem(dict.object(), name))
            return res;
        if (PyErr_Occurred()) {
            *error = -1;
            return nullptr;
        }
    }
    return nullptr;
}

/* Internal API to look for a name through the MRO.
   This returns a borrowed reference, and doesn't set an exception! */
PyObject *
_PepType_Lookup(PyTypeObject *type, PyObject *name)
{
    /* We may end up clearing live exceptions below, so make sure it's ours. */
    assert(!PyErr_Occurred());

    int error{};
    auto *res = find_name_in_mro(type, name, &error);
    /* Only put nullptr results into cache if there was no error. */
    if (error) {
        /* It's not ideal to clear the error condition, but this function is
         * documented as not setting an exception, and I don't want to change
         * that. E.g., when PyType_Ready() can't proceed, it won't set the
         * "ready" flag, so future attempts to ready the same type will call
         * it again -- hopefully in a context that propagates the exception out. */
        if (error == -1)
            PyErr_Clear();
        return nullptr;
    }
    return res;
}

#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for unicodeobject.h
 *
 */
#ifdef Py_LIMITED_API

// structs and macros modelled after their equivalents in
// cpython/Include/cpython/unicodeobject.h

struct PepASCIIObject // since 3.12
{
    PyObject_HEAD
    Py_ssize_t length;          /* Number of code points in the string */
    Py_hash_t hash;             /* Hash value; -1 if not set */
    struct {
        unsigned int interned:2;
        unsigned int kind:3;
        unsigned int compact:1;
        unsigned int ascii:1;
        unsigned int ready:1;
        unsigned int :24;
    } state;
};

struct PepASCIIObject_311 : public PepASCIIObject
{
    wchar_t *wstr;              /* wchar_t representation (null-terminated) */
};

struct PepCompactUnicodeObject // since 3.12
{
    PepASCIIObject _base;
    Py_ssize_t utf8_length;
    char *utf8;                 /* UTF-8 representation (null-terminated) */
};

struct PepCompactUnicodeObject_311 // since 3.12
{
    PepASCIIObject_311 _base;
    Py_ssize_t utf8_length;
    char *utf8;                 /* UTF-8 representation (null-terminated) */
    Py_ssize_t wstr_length;     /* Number of code points in wstr */
};

struct PepUnicodeObject // since 3.12
{
    PepCompactUnicodeObject _base;
    union {
        void *any;
        Py_UCS1 *latin1;
        Py_UCS2 *ucs2;
        Py_UCS4 *ucs4;
    } data;                     /* Canonical, smallest-form Unicode buffer */
};

struct PepUnicodeObject_311
{
    PepCompactUnicodeObject_311 _base;
    union {
        void *any;
        Py_UCS1 *latin1;
        Py_UCS2 *ucs2;
        Py_UCS4 *ucs4;
    } data;                     /* Canonical, smallest-form Unicode buffer */
};

int _PepUnicode_KIND(PyObject *str)
{
    return reinterpret_cast<PepASCIIObject *>(str)->state.kind;
}

int _PepUnicode_IS_ASCII(PyObject *str)
{
    auto *asciiObj = reinterpret_cast<PepASCIIObject *>(str);
    return asciiObj->state.ascii;
}

int _PepUnicode_IS_COMPACT(PyObject *str)
{
    auto *asciiObj = reinterpret_cast<PepASCIIObject *>(str);
    return asciiObj->state.compact;
}

static void *_PepUnicode_ASCII_DATA(PyObject *str)
{
    if (_PepRuntimeVersion() < 0x030C00) {
        auto *asciiObj_311 = reinterpret_cast<PepASCIIObject_311 *>(str);
        return asciiObj_311 + 1;
    }
    auto *asciiObj = reinterpret_cast<PepASCIIObject *>(str);
    return asciiObj + 1;
}

static void *_PepUnicode_COMPACT_DATA(PyObject *str)
{
    if (_PepUnicode_IS_ASCII(str) != 0)
        return _PepUnicode_ASCII_DATA(str);
    if (_PepRuntimeVersion() < 0x030C00) {
        auto *compactObj_311 = reinterpret_cast<PepCompactUnicodeObject_311 *>(str);
        return compactObj_311 + 1;
    }
    auto *compactObj = reinterpret_cast<PepCompactUnicodeObject *>(str);
    return compactObj + 1;
}

static void *_PepUnicode_NONCOMPACT_DATA(PyObject *str)
{
    return _PepRuntimeVersion() < 0x030C00
        ? reinterpret_cast<PepUnicodeObject_311 *>(str)->data.any
        : reinterpret_cast<PepUnicodeObject *>(str)->data.any;
}

void *_PepUnicode_DATA(PyObject *str)
{
    return _PepUnicode_IS_COMPACT(str)
           ? _PepUnicode_COMPACT_DATA(str) : _PepUnicode_NONCOMPACT_DATA(str);
}
#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for pydebug.h
 *
 */
#ifdef Py_LIMITED_API

int
Pep_GetFlag(const char *name)
{
    static PyObject *sys_flags = nullptr;
    static int initialized = 0;

    if (!initialized) {
        sys_flags = PySys_GetObject("flags");
        // func gives no error if nullptr is returned and does not incref.
        Py_XINCREF(sys_flags);
        initialized = 1;
    }
    if (sys_flags != nullptr) {
        Shiboken::AutoDecRef ob_ret(PyObject_GetAttrString(sys_flags, name));
        if (!ob_ret.isNull())
            return int(PyLong_AsLong(ob_ret.object()));
    }
    return -1;
}

int
Pep_GetVerboseFlag()
{
    static int initialized = 0;
    static int verbose_flag = -1;

    if (!initialized) {
        verbose_flag = Pep_GetFlag("verbose");
        if (verbose_flag != -1)
            initialized = 1;
    }
    return verbose_flag;
}
#endif // Py_LIMITED_API

// Support for pyerrors.h

#ifdef PEP_OLD_ERR_API
struct PepException_HEAD
{
    PyObject_HEAD
    PyObject *x1; // dict
    PyObject *args;
};

// PyException_GetArgs/PyException_SetArgs were added to the stable API in 3.12
PyObject *PepException_GetArgs(PyObject *ex)
{
    auto *h = reinterpret_cast<PepException_HEAD *>(ex);
    Py_XINCREF(h->args);
    return h->args;
}

LIBSHIBOKEN_API void PepException_SetArgs(PyObject *ex, PyObject *args)
{
    auto *h = reinterpret_cast<PepException_HEAD *>(ex);
    Py_XINCREF(args);
    auto *old = h->args; // Py_XSETREF()
    h->args = args;
    Py_XDECREF(old);

}
#endif // Limited or < 3.12

/*****************************************************************************
 *
 * Support for code.h
 *
 */
#ifdef Py_LIMITED_API

int
PepCode_Get(PepCodeObject *co, const char *name)
{
    auto *ob = reinterpret_cast<PyObject *>(co);
    Shiboken::AutoDecRef ob_ret(PyObject_GetAttrString(ob, name));
    return ob_ret.isNull() ? -1 : int(PyLong_AsLong(ob_ret.object()));
}

int PepCode_Check(PyObject *o)
{
    return o != nullptr && std::strcmp(Py_TYPE(o)->tp_name, "code") == 0 ? 1 : 0;
}

#endif // Py_LIMITED_API

#if defined(Py_LIMITED_API) || defined(PYPY_VERSION)
PyObject *PepFunction_GetDefaults(PyObject *function)
{
    auto *ob_ret = PyObject_GetAttrString(function, "__defaults__");
    Py_XDECREF(ob_ret); // returns borrowed ref
    return ob_ret != Py_None ? ob_ret : nullptr;
}

#endif // defined(Py_LIMITED_API) || defined(PYPY_VERSION)

/*****************************************************************************
 *
 * Support for datetime.h
 *
 */
#ifdef Py_LIMITED_API

datetime_struc *PyDateTimeAPI = nullptr;

static PyTypeObject *dt_getCheck(const char *name)
{
    PyObject *op = PyObject_GetAttrString(PyDateTimeAPI->module, name);
    if (op == nullptr) {
        fprintf(stderr, "datetime.%s not found\n", name);
        Py_FatalError("libshiboken: error initializing DateTime support, aborting");
    }
    return reinterpret_cast<PyTypeObject *>(op);
}

// init_DateTime is called earlier than our module init.
// We use the provided PyDateTime_IMPORT machinery.
datetime_struc *
init_DateTime(void)
{
    static int initialized = 0;
    if (!initialized) {
        PyDateTimeAPI = (datetime_struc *)malloc(sizeof(datetime_struc));
        if (PyDateTimeAPI == nullptr)
            Py_FatalError("libshiboken: PyDateTimeAPI malloc error, aborting");
        PyDateTimeAPI->module = PyImport_ImportModule("datetime");
        if (PyDateTimeAPI->module == nullptr)
            Py_FatalError("libshiboken: datetime module not found, aborting");
        PyDateTimeAPI->DateType     = dt_getCheck("date");
        PyDateTimeAPI->DateTimeType = dt_getCheck("datetime");
        PyDateTimeAPI->TimeType     = dt_getCheck("time");
        PyDateTimeAPI->DeltaType    = dt_getCheck("timedelta");
        PyDateTimeAPI->TZInfoType   = dt_getCheck("tzinfo");
        initialized = 1;
    }
    return PyDateTimeAPI;
}

int
PyDateTime_Get(PyObject *ob, const char *name)
{
    Shiboken::AutoDecRef ob_ret(PyObject_GetAttrString(ob, name));
    return ob_ret.isNull() ? -1 : int(PyLong_AsLong(ob_ret.object()));
}

PyObject *
PyDate_FromDate(int year, int month, int day)
{
    auto *ob = reinterpret_cast<PyObject *>(PyDateTimeAPI->DateType);
    return PyObject_CallFunction(ob, "(iii)", year, month, day);
}

PyObject *
PyDateTime_FromDateAndTime(int year, int month, int day,
                           int hour, int min, int sec, int usec)
{
    auto *ob = reinterpret_cast<PyObject *>(PyDateTimeAPI->DateTimeType);
    return PyObject_CallFunction(ob, "(iiiiiii)", year, month, day,
                                 hour, min, sec, usec);
}

PyObject *
PyTime_FromTime(int hour, int min, int sec, int usec)
{
    return PyObject_CallFunction((PyObject *)PyDateTimeAPI->TimeType,
                                 (char *)"(iiii)", hour, min, sec, usec);
}
#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for pythonrun.h
 *
 */
#ifdef Py_LIMITED_API

// Flags are ignored in these simple helpers.
PyObject *
PyRun_String(const char *str, int start, PyObject *globals, PyObject *locals)
{
    Shiboken::AutoDecRef code(Py_CompileString(str, "pyscript", start));
    return code.isNull() ? nullptr : PyEval_EvalCode(code.object(), globals, locals);
}

#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for classobject.h
 *
 */
#ifdef Py_LIMITED_API

PyTypeObject *PepMethod_TypePtr = nullptr;

static PyTypeObject *getMethodType(void)
{
    static const char prog[] = R"(class _C:
    def _m(self): pass
result = type(_C()._m)
)";
    return reinterpret_cast<PyTypeObject *>(PepRun_GetResult(prog));
}

// We have no access to PyMethod_New and must call types.MethodType, instead.
PyObject *
PyMethod_New(PyObject *func, PyObject *self)
{
    auto *ob = reinterpret_cast<PyObject *>(PepMethod_TypePtr);
    return PyObject_CallFunction(ob, "(OO)", func, self);
}

PyObject *
PyMethod_Function(PyObject *im)
{
    // We have to return a borrowed reference.
    PyObject *ret = PyObject_GetAttr(im, Shiboken::PyMagicName::func());
    Py_DECREF(ret);
    return ret;
}

PyObject *
PyMethod_Self(PyObject *im)
{
    // We have to return a borrowed reference.
    // If we don't obey that here, then we get a test error!
    PyObject *ret = PyObject_GetAttr(im, Shiboken::PyMagicName::self());
    Py_DECREF(ret);
    return ret;
}
#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for funcobject.h
 *
 */
#ifdef Py_LIMITED_API

PyObject *
PepFunction_Get(PyObject *ob, const char *name)
{
    // We have to return a borrowed reference.
    PyObject *ret = PyObject_GetAttrString(ob, name);
    Py_XDECREF(ret);
    return ret;
}

// This became necessary after Windows was activated.

PyTypeObject *PepFunction_TypePtr = nullptr;

static PyTypeObject *getFunctionType(void)
{
    static const char prog[] =
        "from types import FunctionType as result\n";
    return reinterpret_cast<PyTypeObject *>(PepRun_GetResult(prog));
}
#endif // Py_LIMITED_API

/*****************************************************************************
 *
 * Support for dictobject.h
 *
 */

/*****************************************************************************
 *
 * Extra support for signature.cpp
 *
 */
#ifdef Py_LIMITED_API

PyTypeObject *PepStaticMethod_TypePtr = nullptr;

static PyTypeObject *
getStaticMethodType(void)
{
    static const char prog[] =
        "result = type(str.__dict__['maketrans'])\n";
    return reinterpret_cast<PyTypeObject *>(PepRun_GetResult(prog));
}

typedef struct {
    PyObject_HEAD
    PyObject *sm_callable;
    PyObject *sm_dict;
} staticmethod;

PyObject *
PyStaticMethod_New(PyObject *callable)
{
    staticmethod *sm = (staticmethod *)
        PyType_GenericAlloc(PepStaticMethod_TypePtr, 0);
    if (sm != nullptr) {
        Py_INCREF(callable);
        sm->sm_callable = callable;
    }
    return reinterpret_cast<PyObject *>(sm);
}
#endif // Py_LIMITED_API

#ifdef PYPY_VERSION
PyTypeObject *PepBuiltinMethod_TypePtr = nullptr;

static PyTypeObject *
getBuiltinMethodType(void)
{
    // PYSIDE-535: PyPy has a special builtin method that acts almost like PyCFunction.
    //
    // There is no public declaration for the "builtin method" type.
    // We also cannot grep it with a Python script since the import is too early.
    // Pick a demo "builtin method" by using the VoidPtr type.
    // Create the equivalent of
    // "from shiboken6.Shiboken import VoidPtr\n"
    // "result = type(VoidPtr(0).toBytes)\n";
    auto *pyVoidP = reinterpret_cast<PyObject *>(SbkVoidPtr_TypeF());
    Shiboken::AutoDecRef arg(Py_BuildValue("i", 0));
    Shiboken::AutoDecRef inst(PyObject_CallFunctionObjArgs(pyVoidP, arg.object(), nullptr));
    Shiboken::AutoDecRef meth(PyObject_GetAttrString(inst, "toBytes"));
    auto *result = reinterpret_cast<PyTypeObject *>(PyObject_Type(meth));
    return result;
}
#endif

/*****************************************************************************
 *
 * Common newly needed functions
 *
 */

// The introduction of heaptypes converted many type names to the
// dotted form, since PyType_FromSpec uses it to compute the module
// name. This function reverts this effect.
const char *
PepType_GetNameStr(PyTypeObject *type)
{
    const char *ret = type->tp_name;
    const char *nodots = std::strrchr(ret, '.');
    return nodots != nullptr ? nodots + 1 : ret;
}

const char *PepType_GetFullyQualifiedNameStr(PyTypeObject *type)
{
#if 0
    // Should look like the below code for Limited API >= 3.13, however, it crashes for heap types
    //  since PyType_GetFullyQualifiedName() calls type_qualname() at Objects/typeobject.c:1402
    // which expects a PyHeapTypeObject (struct _heaptypeobject).
    Shiboken::AutoDecRef name(PyType_GetFullyQualifiedName(type));
    Py_ssize_t size{};
    const char *result = PyUnicode_AsUTF8AndSize(name.object(), &size);
    assert(result != nullptr && size > 0);
    return result;
#else
    return type->tp_name;
#endif
}

PyObject *PepType_GetQualName(PyTypeObject *type)
{
#if !defined(PYPY_VERSION) && ((!defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030B0000) || (defined(Py_LIMITED_API) && Py_LIMITED_API >= 0x030B0000))
    return PyType_GetQualName(type);
#else
    return PyObject_GetAttr(reinterpret_cast<PyObject *>(type), Shiboken::PyMagicName::qualname());
#endif
}

// PYSIDE-2264: Find the _functools or functools module and retrieve the
//              partial function. This can be tampered with, check carefully.
/*!
    \fn PyObject *Pep_GetPartialFunction()

    Returns a new reference to functools.partial. On free-threaded builds, a
    second thread waits detached for the first lookup.
*/
PyObject *
Pep_GetPartialFunction(void)
{
    static bool initialized = false;
    static PyObject *result{};
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceFlag once;
    Shiboken::OnceGate gate(once, [] { return initialized; });
#endif
    if (initialized) {
        Py_INCREF(result);
        return result;
    }
    Shiboken::AutoDecRef functools(PyImport_ImportModule("_functools"));
    if (functools.isNull()) {
        PyErr_Clear();
        functools.reset(PyImport_ImportModule("functools"));
    }
    if (functools.isNull())
        Py_FatalError("libshiboken: functools cannot be found");
    result = PyObject_GetAttrString(functools, "partial");
    if (!result || !PyCallable_Check(result))
        Py_FatalError("libshiboken: partial not found or not a function");
    initialized = true;
    return result;
}

/*****************************************************************************
 *
 * Newly introduced convenience functions
 *
 */

// 2020-06-16: For simplicity of creating arbitrary things, this function
// is now made public.

PyObject *
PepRun_GetResult(const char *command)
{
    /*
     * Evaluate a string and return the variable `result`
     */
    PyObject *d = PyDict_New();
    if (d == nullptr)
        return nullptr;

    Shiboken::AutoDecRef builtins(PepEval_GetFrameBuiltins());
    if (PyDict_SetItem(d, Shiboken::PyMagicName::builtins(), PyEval_GetBuiltins()) < 0)
        return nullptr;
    builtins.reset(nullptr);

    PyObject *v = PyRun_String(command, Py_file_input, d, d);
    // PyDict_GetItem returns a borrowed reference owned by d; increment before
    // releasing d so the returned pointer remains valid.
    PyObject *res = v ? PyDict_GetItem(d, Shiboken::PyName::result()) : nullptr;
    Py_XINCREF(res);
    Py_XDECREF(v);
    Py_DECREF(d);
    return res;
}

PyTypeObject *PepType_Type_tp_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    auto *ret = PyType_Type.tp_new(metatype, args, kwds);
    return reinterpret_cast<PyTypeObject *>(ret);
}

/*****************************************************************************
 *
 * Extra support for name mangling: "__private" becomes "_classname__private".
 * Those functions are modelled after _Py_Mangle, but are optimized a little for
 * our purpose. */

bool _Pep_IsPrivateName(PyObject *name)
{
    if (PyUnicode_ReadChar(name, 0) != '_' || PyUnicode_ReadChar(name, 1) != '_')
        return false;
    const Py_ssize_t nlen = PyUnicode_GetLength(name);
    /* Don't mangle __id__ or names with dots. */
    if ((PyUnicode_ReadChar(name, nlen-1) == '_' &&
         PyUnicode_ReadChar(name, nlen-2) == '_') ||
        PyUnicode_FindChar(name, '.', 0, nlen, 1) != -1) {
        return false;
    }
    return true;
}

PyObject *_Pep_TypePrivateMangle(PyTypeObject *obType, PyObject *name)
{
    Shiboken::AutoDecRef privateobj(PyObject_GetAttr(
        reinterpret_cast<PyObject *>(obType), Shiboken::PyMagicName::name()));

    const Py_ssize_t nlen = PyUnicode_GetLength(name);
    // PYSIDE-1436: _Py_Mangle is no longer exposed; implement it always.
    // The rest of this function is our own implementation of _Py_Mangle.
    // Please compare the original function in compile.c .
    Py_ssize_t plen = PyUnicode_GetLength(privateobj.object());
    /* Strip leading underscores from class name */
    Py_ssize_t ipriv = 0;
    while (PyUnicode_ReadChar(privateobj.object(), ipriv) == '_')
        ipriv++;
    if (ipriv == plen) {
        Py_INCREF(name);
        return name; /* Don't mangle if class is just underscores */
    }
    plen -= ipriv;

    if (plen + nlen >= PY_SSIZE_T_MAX - 1) {
        PyErr_SetString(PyExc_OverflowError,
                        "private identifier too large to be mangled");
        return nullptr;
    }
    const Py_ssize_t amount = ipriv + 1 + plen + nlen;
    const Py_ssize_t big_stack = 1000;
    wchar_t bigbuf[big_stack];
    wchar_t *resbuf = amount <= big_stack
        ? bigbuf : reinterpret_cast<wchar_t *>(malloc(sizeof(wchar_t) * amount));
    if (!resbuf)
        return nullptr;
    /* ident = "_" + priv[ipriv:] + ident # i.e. 1+plen+nlen bytes */
    resbuf[0] = '_';
    if (PyUnicode_AsWideChar(privateobj, resbuf + 1, ipriv + plen) < 0) {
        if (amount > big_stack)
            free(resbuf);
        return nullptr;
    }
    if (PyUnicode_AsWideChar(name, resbuf + ipriv + plen + 1, nlen) < 0) {
        if (amount > big_stack)
            free(resbuf);
        return nullptr;
    }
    PyObject *result = PyUnicode_FromWideChar(resbuf + ipriv, 1 + plen + nlen);
    if (amount > big_stack)
        free(resbuf);
    return result;
}

PyObject *_Pep_PrivateMangle(PyObject *self, PyObject *name)
{
    if (!_Pep_IsPrivateName(name)) {
        Py_INCREF(name);
        return name;
    }
    return _Pep_TypePrivateMangle(Py_TYPE(self), name);
}

/*****************************************************************************
 *
 * Runtime support for Python 3.8 incompatibilities
 *
 */

static long _GetPepRuntimeVersion()
{
    auto *version = PySys_GetObject("version_info");
    const auto major = PyLong_AsLong(PyTuple_GetItem(version, 0));
    const auto minor = PyLong_AsLong(PyTuple_GetItem(version, 1));
    const auto micro = PyLong_AsLong(PyTuple_GetItem(version, 2));
    return major << 16 | minor << 8 | micro;
}

long _PepRuntimeVersion()
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<const long *> number;
    return *number.get([] { return new long(_GetPepRuntimeVersion()); });
#else
    static const auto number = _GetPepRuntimeVersion();
    return number;
#endif
}

/*****************************************************************************
 *
 * PYSIDE-535: Support for PyPy
 *
 * This has the nice side effect of a more clean implementation,
 * and we don't keep the old macro version.
 *
 */

///////////////////////////////////////////////////////////////////////
//
// PEP 697: Support for embedded type structures.
//
// According to `https://docs.python.org/3/c-api/object.html?highlight=pyobject_gettypedata#c.PyObject_GetTypeData`
// the function `PyObject_GetTypeData` should belong to the Stable API
// since version 3.12.0, but it does not. We use instead some copies
// from Python source code.

#if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030C0000

# define PepObject_GetTypeData PyObject_GetTypeData

SbkObjectTypePrivate *PepType_SOTP(PyTypeObject *type)
{
    // PYSIDE-2676: Use the meta type explicitly.
    //              A derived type would fail the offset calculation.
    assert(SbkObjectType_Check(type));
    auto *obType = reinterpret_cast<PyObject *>(type);
    void *data = PyObject_GetTypeData(obType, SbkObjectType_TypeF());
    return reinterpret_cast<SbkObjectTypePrivate *>(data);
}

void PepType_SOTP_delete(PyTypeObject * /*type*/)
{
}

#else

// The following comments are directly copied from Python 3.12
//

// Make sure we have maximum alignment, even if the current compiler
// does not support max_align_t. Note that:
// - Autoconf reports alignment of unknown types to 0.
// - 'long double' has maximum alignment on *most* platforms,
//   looks like the best we can do for pre-C11 compilers.
// - The value is tested, see test_alignof_max_align_t
#  if !defined(ALIGNOF_MAX_ALIGN_T) || ALIGNOF_MAX_ALIGN_T == 0
#    undef ALIGNOF_MAX_ALIGN_T
#    define ALIGNOF_MAX_ALIGN_T alignof(long double)
#  endif

/* Align up to the nearest multiple of alignof(max_align_t)
 * (like _Py_ALIGN_UP, but for a size rather than pointer)
 */
static Py_ssize_t _align_up(Py_ssize_t size)
{
    return (size + ALIGNOF_MAX_ALIGN_T - 1) & ~(ALIGNOF_MAX_ALIGN_T - 1);
}

static void *PepObject_GetTypeData(PyObject *obj, PyTypeObject *cls)
{
    assert(PyObject_TypeCheck(obj, cls));
    return reinterpret_cast<char *>(obj) + _align_up(cls->tp_base->tp_basicsize);
}
//
///////////////////////////////////////////////////////////////////////

/*
 * PyTypeObject extender
 */

static std::unordered_map<PyTypeObject *, SbkObjectTypePrivate > SOTP_extender{};
static thread_local PyTypeObject *SOTP_key{};
static thread_local SbkObjectTypePrivate *SOTP_value{};

SbkObjectTypePrivate *PepType_SOTP(PyTypeObject *type)
{
    static bool use_312 = _PepRuntimeVersion() >= 0x030C00;
    assert(SbkObjectType_Check(type));
    if (use_312) {
        auto *obType = reinterpret_cast<PyObject *>(type);
        void *data = PepObject_GetTypeData(obType, SbkObjectType_TypeF());
        return reinterpret_cast<SbkObjectTypePrivate *>(data);
    }
    if (type == SOTP_key)
        return SOTP_value;
    auto it = SOTP_extender.find(type);
    if (it == SOTP_extender.end()) {
        it = SOTP_extender.insert({type, {}}).first;
        memset(&it->second, 0, sizeof(SbkObjectTypePrivate));
    }
    SOTP_key = type;
    SOTP_value = &it->second;
    return SOTP_value;
}

void PepType_SOTP_delete(PyTypeObject *type)
{
    static bool use_312 = _PepRuntimeVersion() >= 0x030C00;
    assert(SbkObjectType_Check(type));
    if (use_312)
        return;
    SOTP_extender.erase(type);
    SOTP_key = nullptr;
}

#endif // !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030C0000

/*
 * SbkEnumType extender
 */
using SETP_Extender = std::unordered_map<SbkEnumType *, SbkEnumTypePrivate>;

static SETP_Extender &getSETP_extender()
{
    static SETP_Extender result;
    return result;
}

static thread_local SbkEnumType *SETP_key{};
static thread_local SbkEnumTypePrivate *SETP_value{};

#ifdef Py_GIL_DISABLED
static std::mutex &setpMutex()
{
    static std::mutex mutex;
    return mutex;
}

/*!
    \class SetpGuard
    \ingroup freethreading
    \inmodule libshiboken

    Imports and QEnum classes in any thread create enums; conversions read
    their private data. Map work only.
*/
class SetpGuard
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(SetpGuard)

    SetpGuard()
    {
        if (Shiboken::FreeThreading::optionEnabled(
                Shiboken::FreeThreading::Option::EnumPrivateLock))
            m_guard.emplace(setpMutex());
    }

    ~SetpGuard() = default;

private:
    std::optional<Shiboken::TrackedGuard<std::mutex, Shiboken::RawLock::EnumPrivate>> m_guard;
};
#endif // Py_GIL_DISABLED

SbkEnumTypePrivate *PepType_SETP(SbkEnumType *enumType)
{
    // PYSIDE-2230: This makes no sense at all for Enum types.
    if (enumType == SETP_key)
        return SETP_value;
    auto &SETP_extender = getSETP_extender();
#ifdef Py_GIL_DISABLED
    // The node is stable; the cache keeps a pointer into it
    SbkEnumTypePrivate *value = [&SETP_extender, enumType] {
        SetpGuard guard;
        auto it = SETP_extender.find(enumType);
        if (it == SETP_extender.end())
            it = SETP_extender.insert({enumType, SbkEnumTypePrivate{nullptr, nullptr}}).first;
        return &it->second;
    }();
    SETP_key = enumType;
    SETP_value = value;
#else
    auto it = SETP_extender.find(enumType);
    if (it == SETP_extender.end())
        it = SETP_extender.insert({enumType, SbkEnumTypePrivate{nullptr, nullptr}}).first;
    SETP_key = enumType;
    SETP_value = &it->second;
#endif // Py_GIL_DISABLED
    return SETP_value;
}

void PepType_SETP_delete(SbkEnumType *enumType)
{
#ifdef Py_GIL_DISABLED
    {
        SetpGuard guard;
        getSETP_extender().erase(enumType);
    }
#else
    getSETP_extender().erase(enumType);
#endif
    SETP_key = nullptr;
}

#ifdef Py_LIMITED_API
static PyObject *emulatePyType_GetDict(PyTypeObject *type)
{
    if (_PepRuntimeVersion() < 0x030C00 || type->tp_dict) {
        auto *res = type->tp_dict;
        Py_XINCREF(res);
        return res;
    }
    // PYSIDE-2230: Here we are really cheating. We don't know how to
    //              access an internal dict, and so we simply pretend
    //              it were an empty dict. This works great for our types.
    // This was an unexpectedly simple solution :D
    return PyDict_New();
}
#endif

// PyType_GetDict: replacement for <static type>.tp_dict, which is
// zero for builtin types since 3.12.
PyObject *PepType_GetDict(PyTypeObject *type)
{
#if !defined(Py_LIMITED_API)
#  if PY_VERSION_HEX >= 0x030C0000
    return PyType_GetDict(type);
#  else
    // pre 3.12 fallback code, mimicking the addref-behavior.
    Py_XINCREF(type->tp_dict);
    return type->tp_dict;
#  endif
#else
    return emulatePyType_GetDict(type);
#endif // Py_LIMITED_API
}

int PepType_SetDict(PyTypeObject *type, PyObject *dict)
{
    type->tp_dict = dict;
    // Invalidate the per-type attribute cache so Python 3.12+ specialised
    // bytecode does not continue serving stale lookups from the old dict.
    PyType_Modified(type);
    return 0;
}

PyObject *PepEval_GetFrameGlobals()
{
    // PyEval_GetFrameGlobals() (added to stable ABI in 3.13) returns a new reference
    // as opposed to deprecated PyEval_GetGlobals() which returns a borrowed reference
#if !defined(PYPY_VERSION) && ((!defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030D0000) || (defined(Py_LIMITED_API) && Py_LIMITED_API >= 0x030D0000))
    return PyEval_GetFrameGlobals();
#else
    PyObject *result = PyEval_GetGlobals();
    Py_XINCREF(result);
    return result;
#endif
}

PyObject *PepEval_GetFrameBuiltins()
{
    // PepEval_GetFrameBuiltins() (added to stable ABI in 3.13) returns a new reference
    // as opposed to deprecated PyEval_GetBuiltins() which returns a borrowed reference
#if !defined(PYPY_VERSION) && ((!defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030D0000) || (defined(Py_LIMITED_API) && Py_LIMITED_API >= 0x030D0000))
    return PyEval_GetFrameBuiltins();
#else
    PyObject *result = PyEval_GetBuiltins();
    Py_XINCREF(result);
    return result;
#endif
}

int PepModule_AddType(PyObject *module, PyTypeObject *type)
{
    // PyModule_AddType (added to stable ABI in 3.10) is the replacement for
    // PyModule_AddObject() (deprecated in 3.13) for adding types to a module.
#ifndef PYPY_VERSION
    return PyModule_AddType(module, type);
#else
    auto *ob = reinterpret_cast<PyObject *>(type);
    int result = PyModule_AddObject(module, PepType_GetNameStr(type), ob);
    if (result != 0)
        Py_XDECREF(ob);
    return result;
#endif
}

int PepModule_Add(PyObject *module, const char *name, PyObject *value)
{
    // PyModule_Add (added to stable ABI in 3.13) is the replacement for PyModule_AddObject()
    // (deprecated in 3.13).
#if !defined(PYPY_VERSION) && ((!defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030D0000) || (defined(Py_LIMITED_API) && Py_LIMITED_API >= 0x030D0000))
    return PyModule_Add(module, name, value);
#else
    int result = PyModule_AddObject(module, name, value);
    if (result != 0)
        Py_XDECREF(value);
    return result;
#endif
}

#ifdef Py_GIL_DISABLED
/*!
    \fn PyObject *PepDict_IterationSnapshot(PyObject *dict)
    \ingroup freethreading
    \inmodule libshiboken

    Returns a new reference to a copy of \a dict for a PyDict_Next() loop,
    so that another thread changing \a dict cannot free the borrowed items.
    nullptr with an exception on failure.
*/
PyObject *PepDict_IterationSnapshot(PyObject *dict)
{
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::DictIterationSnapshot))
        return Py_NewRef(dict);
    if (PyDict_CheckExact(dict))
        return PyDict_Copy(dict);
    // PyDict_Copy() of a subclass goes through its keys() and __getitem__
    PyObject *copy = PyDict_New();
    if (copy == nullptr)
        return nullptr;
    int failed = 0;
    Py_BEGIN_CRITICAL_SECTION(dict);
    PyObject *key{};
    PyObject *value{};
    Py_ssize_t pos = 0;
    while (failed == 0 && PyDict_Next(dict, &pos, &key, &value))
        failed = PyDict_SetItem(copy, key, value);
    Py_END_CRITICAL_SECTION();
    if (failed != 0)
        Py_CLEAR(copy);
    return copy;
}
#endif // Py_GIL_DISABLED

/***************************************************************************
 *
 * PYSIDE-535: The enum/flag error
 * -------------------------------
 *
 * This is a fragment of the code which was used to find the enum/flag
 * alias error. See the change to `setTypeConverter` in sbkenum.cpp .
 *

Usage:

python3 -c "from PySide6 import QtCore" 2>&1 | python3 tools/debug_renamer.py | uniq -c | head -10

   5 PepType_ExTP:940 x_A SOTP s=96
   4 PepType_ExTP:940 x_B SETP s=24
   2 PepType_ExTP:940 x_C PFTP s=16
   4 PepType_ExTP:940 x_D SETP s=24
   1 PepType_ExTP:940 x_C SETP s=24
   2 PepType_ExTP:940 x_E PFTP s=16
   4 PepType_ExTP:940 x_F SETP s=24
   1 PepType_ExTP:940 x_E SETP s=24
   4 PepType_ExTP:940 x_G SETP s=24
   4 PepType_ExTP:940 x_H SETP s=24

static inline void *PepType_ExTP(PyTypeObject *type, size_t size)
{
    static const char *env_p = std::getenv("PFTP");
    if (env_p) {
        static PyTypeObject *alias{};
        const char *kind = size == sizeof(SbkObjectTypePrivate) ? "SOTP" :
                           size == sizeof(SbkEnumTypePrivate) ? "SETP" :
                           size == sizeof(SbkQFlagsTypePrivate) ? "PFTP" :
                           "unk.";
        fprintf(stderr, "%s:%d %p x %s s=%ld\n", __func__, __LINE__, type, kind, size);
        PyObject *kill{};
        if (strlen(env_p) > 0) {
            if (size == sizeof(SbkQFlagsTypePrivate)) {
                if (alias == nullptr)
                    alias = type;
            }
            if (size != sizeof(SbkQFlagsTypePrivate)) {
                if (type == alias)
                    Py_INCREF(kill);
            }
        }
    }
    const auto ikey = reinterpret_cast<std::uintptr_t>(type);
    if (ikey == cached_key)
        return cached_value;
    auto it = SOTP_extender.find(ikey);
    if (it == SOTP_extender.end()) {
        PepType_ExTP_init(type, size);
        return PepType_ExTP(type, size);
    }
    cached_key = ikey;
    cached_value = reinterpret_cast<void *>(it->second);
    return cached_value;
}
*/

/*****************************************************************************
 *
 * Module Initialization
 *
 */

void
Pep384_Init()
{
#ifdef Py_LIMITED_API
    check_PyTypeObject_valid();
    Pep_GetVerboseFlag();
    PepMethod_TypePtr = getMethodType();
    PepFunction_TypePtr = getFunctionType();
    PepStaticMethod_TypePtr = getStaticMethodType();
#endif // Py_LIMITED_API
#ifdef PYPY_VERSION
    PepBuiltinMethod_TypePtr = getBuiltinMethodType();
#endif
}

} // extern "C"
