// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkenum.h"
#include "sbkenum_p.h"
#include "sbkpep.h"
#include "sbkpytuplerange.h"
#include "sbkstring.h"
#include "helper.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"
#include "sbkconverter.h"
#include "basewrapper_p.h"
#include "autodecref.h"
#include "sbktypefactory.h"

#include <cstring>
#include <iostream>

using namespace Shiboken;

extern "C"
{

struct SbkEnumType
{
    PyTypeObject type;
};

// Initialization
static bool _init_enum()
{
    AutoDecRef shibo(PyImport_ImportModule("shiboken6.Shiboken"));
    return !shibo.isNull();
}

struct EnumGlobalData
{
    PyObject *PyEnumModule{};
    PyTypeObject *PyEnumMeta{};
    PyObject *PyEnum{};
    PyObject *PyIntEnum{};
    PyObject *PyFlag{};
    PyObject *PyIntFlag{};
    PyObject *PyFlag_KEEP{};
};

EnumGlobalData *enumGlobals()
{
    static EnumGlobalData result;
    return &result;
}

bool PyEnumMeta_Check(PyObject *ob)
{
    return Py_TYPE(ob) == enumGlobals()->PyEnumMeta;
}

static bool initEnumGlobals(EnumGlobalData *globals)
{
    auto *mod = PyImport_ImportModule("enum");
    if (mod == nullptr)
        return false;
    globals->PyEnumModule = mod;
    auto *PyEnumMeta = PyObject_GetAttrString(mod, "EnumMeta");
    if (PyEnumMeta == nullptr || PyType_Check(PyEnumMeta) == 0)
        return false;
    globals->PyEnumMeta = reinterpret_cast<PyTypeObject *>(PyEnumMeta);
    globals->PyEnum = PyObject_GetAttrString(mod, "Enum");
    if (globals->PyEnum == nullptr || PyType_Check(globals->PyEnum) == 0)
        return false;
    globals->PyIntEnum = PyObject_GetAttrString(mod, "IntEnum");
    if (globals->PyIntEnum == nullptr || PyType_Check(globals->PyIntEnum) == 0)
        return false;
    globals->PyFlag = PyObject_GetAttrString(mod, "Flag");
    if (globals->PyFlag == nullptr || PyType_Check(globals->PyFlag) == 0)
        return false;
    globals->PyIntFlag = PyObject_GetAttrString(mod, "IntFlag");
    if (globals->PyIntFlag == nullptr || PyType_Check(globals->PyIntFlag) == 0)
        return false;
    // KEEP is defined from Python 3.11 on.
    globals->PyFlag_KEEP = PyObject_GetAttrString(mod, "KEEP");
    PyErr_Clear();
    return true;
}

PyTypeObject *getPyEnumMeta()
{
    auto *globals = enumGlobals();
    if (globals->PyEnumMeta == nullptr && !initEnumGlobals(globals)) {
        PyErr_Print();
        Py_FatalError("libshiboken: Python module 'enum' not found");
        return nullptr;
    }
    return globals->PyEnumMeta;
}

// PYSIDE-1735: Determine whether we should use the old or the new enum implementation.
static int enumOption()
{
    if (PyObject *option = PySys_GetObject("pyside6_option_python_enum")) {
        if (PyLong_Check(option) != 0) {
            int ignoreOver{};
            return PyLong_AsLongAndOverflow(option, &ignoreOver);
        }
    }
    PyErr_Clear();
    return 1;
}

// Called from init_shibokensupport_module().
void init_enum()
{
    static bool isInitialized = false;
    if (isInitialized)
        return;
    if (!(isInitialized || _init_enum()))
        Py_FatalError("libshiboken: could not init enum");

    Enum::enumOption = enumOption();
    getPyEnumMeta();
    isInitialized = true;
}

// PYSIDE-1735: Helper function supporting QEnum
int enumIsFlag(PyObject *ob_type)
{
    init_enum();

    auto *globals = enumGlobals();
    auto *metatype = Py_TYPE(ob_type);
    if (metatype != globals->PyEnumMeta)
        return -1;
    const Shiboken::PyTupleRange mro(reinterpret_cast<PyTypeObject *>(ob_type)->tp_mro);
    return std::find(mro.begin(), mro.end(), globals->PyFlag) != mro.end() ? 1 : 0;
}

///////////////////////////////////////////////////////////////////////
//
// Support for Missing Values
// ==========================
//
// Qt enums sometimes use undefined values in enums.
// The enum module handles this by the option "KEEP" for Flag and
// IntFlag. The handling of missing enum values is still strict.
//
// We changed that (also for compatibility with some competitor)
// and provide a `_missing_` function that creates the missing value.
//
// The idea:
// ---------
// We cannot modify the already created class.
// But we can create a one-element class with the new value and
// pretend that this is the already existing class.
//
// We create each constant only once and keep the result in a dict
// "_sbk_missing_". This is similar to a competitor's "_sip_missing_".
//

static PyObject *missing_func(PyObject * /* self */ , PyObject *args)
{
    // In order to relax matters to be more compatible with C++, we need
    // to create a pseudo-member with that value.
    static auto *const _sbk_missing = Shiboken::String::createStaticString("_sbk_missing_");
    static auto *const _name = Shiboken::String::createStaticString("__name__");
    static auto *const _mro = Shiboken::String::createStaticString("__mro__");
    static auto *const _class = Shiboken::String::createStaticString("__class__");

    PyObject *klass{};
    PyObject *value{};
    if (!PyArg_UnpackTuple(args, "missing", 2, 2, &klass, &value))
        Py_RETURN_NONE;
    if (!PyLong_Check(value))
        Py_RETURN_NONE;
    auto *type = reinterpret_cast<PyTypeObject *>(klass);
    AutoDecRef tpDict(PepType_GetDict(type));
    auto *sbk_missing = PyDict_GetItem(tpDict.object(), _sbk_missing);
    if (!sbk_missing) {
        sbk_missing = PyDict_New();
        PyDict_SetItem(tpDict.object(), _sbk_missing, sbk_missing);
    }
    // See if the value is already in the dict.
    AutoDecRef val_str(PyObject_CallMethod(value, "__str__", nullptr));
    auto *ret = PyDict_GetItem(sbk_missing, val_str);
    if (ret) {
        Py_INCREF(ret);
        return ret;
    }
    // No, we must create a new object and insert it into the dict.
    AutoDecRef cls_name(PyObject_GetAttr(klass, _name));
    AutoDecRef mro(PyObject_GetAttr(klass, _mro));
    auto *baseClass(PyTuple_GetItem(mro, 1));
    AutoDecRef param(PyDict_New());
    PyDict_SetItem(param, val_str, value);
    AutoDecRef fake(PyObject_CallFunctionObjArgs(baseClass, cls_name.object(), param.object(),
                                                 nullptr));
    ret = PyObject_GetAttr(fake, val_str);
    PyDict_SetItem(sbk_missing, val_str, ret);
    // Now the real fake: Pretend that the type is our original type!
    PyObject_SetAttr(ret, _class, klass);
    return ret;
}

static struct PyMethodDef dummy_methods[] = {
    {"_missing_", missing_func, METH_VARARGS|METH_STATIC, nullptr},
    {nullptr, nullptr, 0, nullptr}
};

static PyType_Slot dummy_slots[] = {
    {Py_tp_base, reinterpret_cast<void *>(&PyType_Type)},
    {Py_tp_methods, reinterpret_cast<void *>(dummy_methods)},
    {0, nullptr}
};

static PyType_Spec dummy_spec = {
    "1:builtins.EnumType",
    0,
    0,
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    dummy_slots,
};

static PyObject *create_missing_func(PyObject *klass)
{
    // When creating the class, memorize it in the missing function by
    // a partial function argument.
    static auto *const type = SbkType_FromSpec(&dummy_spec);
    static auto *const obType = reinterpret_cast<PyObject *>(type);
    static auto *const _missing = Shiboken::String::createStaticString("_missing_");
    static auto *const func = PyObject_GetAttr(obType, _missing);
    static auto *const partial = Pep_GetPartialFunction();
    return PyObject_CallFunctionObjArgs(partial, func, klass, nullptr);
}
//
////////////////////////////////////////////////////////////////////////

} // extern "C"

/*!
    \namespace Shiboken::Enum

    \brief Namespace for enum-related functionality.

    \inmodule libshiboken
*/
namespace Shiboken::Enum {

int enumOption{};

bool check(PyObject *pyObj)
{
    return checkType(Py_TYPE(pyObj));
}

bool checkType(PyTypeObject *pyTypeObj)
{
    init_enum();

    return Py_TYPE(reinterpret_cast<PyObject *>(pyTypeObj)) == getPyEnumMeta();
}

PyObject *getEnumItemFromValue(PyTypeObject *enumType, EnumValueType itemValue)
{
    init_enum();

    auto *obEnumType = reinterpret_cast<PyObject *>(enumType);
    AutoDecRef val2members(PyObject_GetAttrString(obEnumType, "_value2member_map_"));
    if (val2members.isNull()) {
        PyErr_Clear();
        return nullptr;
    }
    AutoDecRef ob_value(PyLong_FromLongLong(itemValue));
    auto *result = PyDict_GetItem(val2members, ob_value);
    Py_XINCREF(result);
    return result;
}

PyObject *newItem(PyTypeObject *enumType, EnumValueType itemValue,
                  const char *itemName)
{
    init_enum();

    auto *obEnumType = reinterpret_cast<PyObject *>(enumType);
    if (!itemName)
        return PyObject_CallFunction(obEnumType, "L", itemValue);

    static PyObject *const _member_map_ = String::createStaticString("_member_map_");
    AutoDecRef tpDict(PepType_GetDict(enumType));
    auto *member_map = PyDict_GetItem(tpDict.object(), _member_map_);
    if (!(member_map && PyDict_Check(member_map)))
        return nullptr;
    auto *result = PyDict_GetItemString(member_map, itemName);
    Py_XINCREF(result);
    return result;
}

EnumValueType getValue(PyObject *enumItem)
{
    init_enum();

    assert(Enum::check(enumItem));

    AutoDecRef pyValue(PyObject_GetAttrString(enumItem, "value"));
    return PyLong_AsLongLong(pyValue);
}

void setTypeConverter(PyTypeObject *type, SbkConverter *converter,
                      SbkConverter *flagsConverter)
{
    SbkEnumTypePrivate *priv = PepType_SETP(reinterpret_cast<SbkEnumType *>(type));
    priv->converter = converter;
    priv->flagsConverter = flagsConverter;
}

SbkConverter *getConverter(SbkEnumType *type)
{
    SbkEnumTypePrivate *priv = PepType_SETP(type);
    return priv->converter;
}

SbkConverter *getFlagsConverter(SbkEnumType *type)
{
    SbkEnumTypePrivate *priv = PepType_SETP(type);
    return priv->flagsConverter;
}

static void setModuleAndQualnameOnType(PyObject *type, const char *fullName)
{
    const char *colon = std::strchr(fullName, ':');
    assert(colon);
    int package_level = atoi(fullName);
    const char *mod = colon + 1;

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

    PyObject_SetAttr(type, Shiboken::PyMagicName::module(), module);
    PyObject_SetAttr(type, Shiboken::PyMagicName::qualname(), qualname);
}

struct EnumContext
{
    PyObject *scopeOrModule{};
    const char *fullName{};
    const char *nameC{};
    AutoDecRef name;
    AutoDecRef PyEnumType;
    bool isFlag{false};
};

EnumContext createEnumContext(PyObject *scopeOrModule, const char *fullName)
{
    EnumContext result;
    result.scopeOrModule = scopeOrModule;
    result.fullName = fullName;

    const char *dot = std::strrchr(fullName, '.');
    result.nameC = dot ? dot + 1 : fullName;
    result.name.reset(Shiboken::String::fromCString(result.nameC));

    // A module-scope enum has no entry in a type's enumTypeDict: it is an IntEnum.
    static PyObject *const intEnumName = String::createStaticString("IntEnum");
    PyObject *enumName = intEnumName;
    if (PyType_Check(scopeOrModule)) {
        // For global objects, we have no good solution, yet where to put the int info.
        auto *type = reinterpret_cast<PyTypeObject *>(scopeOrModule);
        auto *sotp = PepType_SOTP(type);
        if (!sotp->enumFlagsDict)
            initEnumFlagsDict(type);
        enumName = PyDict_GetItem(sotp->enumTypeDict, result.name.object());
    }

    SBK_UNUSED(getPyEnumMeta()); // enforce PyEnumModule creation
    auto *globals = enumGlobals();
    assert(globals->PyEnumModule != nullptr);
    result.PyEnumType.reset(PyObject_GetAttr(globals->PyEnumModule, enumName));
    assert(result.PyEnumType.object());
    result.isFlag = PyObject_IsSubclass(result.PyEnumType, globals->PyFlag);

    // See if we should use the Int versions of the types, again
    if ((Enum::enumOption & Enum::ENOPT_INHERIT_INT) != 0) {
        auto *surrogate = result.isFlag ? globals->PyIntFlag : globals->PyIntEnum;
        Py_INCREF(surrogate);
        result.PyEnumType.reset(surrogate);
    }

    return result;
}

static PyTypeObject *createEnumForPython(const EnumContext &enumContext,
                                         PyObject *pyEnumItems)
{
    auto *globals = enumGlobals();

    // See if we should use the Int versions of the types, again

    // Walk the enumItemStrings and create a Python enum type.

    // We now create the new type. Since Python 3.11, we need to pass in
    // `boundary=KEEP` because the default STRICT crashes on us.
    // See  QDir.Filter.Drives | QDir.Filter.Files
    AutoDecRef callArgs(Py_BuildValue("(OO)", enumContext.name.object(), pyEnumItems));
    AutoDecRef callDict(PyDict_New());
    static PyObject *boundary = String::createStaticString("boundary");
    if (globals->PyFlag_KEEP)
        PyDict_SetItem(callDict, boundary, globals->PyFlag_KEEP);
    auto *obNewType = PyObject_Call(enumContext.PyEnumType.object(), callArgs, callDict);
    if (!obNewType || PyObject_SetAttr(enumContext.scopeOrModule, enumContext.name.object(), obNewType) < 0)
        return nullptr;

    // For compatibility with Qt enums, provide a permissive missing method for (Int)?Enum.
    if (!enumContext.isFlag) {
        bool supportMissing = !(Enum::enumOption & Enum::ENOPT_NO_MISSING);
        if (supportMissing) {
            AutoDecRef enum_missing(create_missing_func(obNewType));
            PyObject_SetAttrString(obNewType, "_missing_", enum_missing);
        }
    }

    setModuleAndQualnameOnType(obNewType, enumContext.fullName);

    // See if we should re-introduce shortcuts in the enclosing object.
    const bool useGlobalShortcut = (Enum::enumOption & Enum::ENOPT_GLOBAL_SHORTCUT) != 0;
    const bool useScopedShortcut = (Enum::enumOption & Enum::ENOPT_SCOPED_SHORTCUT) != 0;
    if (useGlobalShortcut || useScopedShortcut) {
        // We have to use the iterator protokol because the values dict is a mappingproxy.
        AutoDecRef values(PyObject_GetAttr(obNewType, PyMagicName::members()));
        AutoDecRef mapIterator(PyObject_GetIter(values));
        AutoDecRef mapKey{};
        bool isModule = PyModule_Check(enumContext.scopeOrModule);
        while ((mapKey.reset(PyIter_Next(mapIterator))), mapKey.object()) {
            if ((useGlobalShortcut && isModule) || (useScopedShortcut && !isModule)) {
                AutoDecRef value(PyObject_GetItem(values, mapKey));
                if (PyObject_SetAttr(enumContext.scopeOrModule, mapKey, value) < 0)
                    return nullptr;
            }
        }
    }

    return reinterpret_cast<PyTypeObject *>(obNewType);
}

template <typename IntT>
static PyObject *toPyObject(IntT v)
{
    if constexpr (sizeof(IntT) == 8) {
        if constexpr (std::is_unsigned_v<IntT>)
            return PyLong_FromUnsignedLongLong(v);
        return PyLong_FromLongLong(v);
    }
    if constexpr (std::is_unsigned_v<IntT>)
        return PyLong_FromUnsignedLong(v);
    return PyLong_FromLong(v);
}

template <typename IntT>
static PyTypeObject *createPythonEnumHelper(const EnumContext &enumContext,
                                            const char *enumItemStrings[], const IntT enumValues[])
{
    AutoDecRef args(PyList_New(0));
    auto *pyEnumItems = args.object();
    for (size_t idx = 0; enumItemStrings[idx] != nullptr; ++idx) {
        const char *kv = enumItemStrings[idx];
        auto *key = PyUnicode_FromString(kv);
        auto *value = toPyObject(enumValues[idx]);
        auto *key_value = PyTuple_New(2);
        PyTuple_SetItem(key_value, 0, key);
        PyTuple_SetItem(key_value, 1, value);
        PyList_Append(pyEnumItems, key_value);
    }
    return createEnumForPython(enumContext, pyEnumItems);
}

// Now we have to concretize these functions explicitly,
// otherwise templates will not work across modules.

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const int64_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const uint64_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const int32_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const uint32_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const int16_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const uint16_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const int8_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

PyTypeObject *createPythonEnum(PyObject *module,
    const char *fullName, const char *enumItemStrings[], const uint8_t enumValues[])
{
    const EnumContext context = createEnumContext(module, fullName);
    return createPythonEnumHelper(context, enumItemStrings, enumValues);
}

/*!
    Creates a Python enum type from a set of provided key/value pairs.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a fullName
            \li The full name (including module and package depth) to be used
                for the newly created enum type.
        \row
            \li \a pyEnumItems
            \li The key/value pairs to be used for the enum.
        \row
            \li \a enumTypeName
            \li The name of the enum type to be used (i.e., "IntEnum", "Flag", etc)
                from Python's enum module.
        \row
            \li \a callDict
            \li The dictionary to be used for the call, allowing for additional
                keyword arguments to be passed, such as "boundary=KEEP".
    \endtable
*/
PyTypeObject *createPythonEnum(const char *fullName, PyObject *pyEnumItems,
                               const char *enumTypeName, PyObject *callDict)
{
    SBK_UNUSED(getPyEnumMeta());
    AutoDecRef PyEnumTypeName(Shiboken::String::fromCString(enumTypeName));
    AutoDecRef PyEnumType(PyObject_GetAttr(enumGlobals()->PyEnumModule, PyEnumTypeName));
    if (!PyEnumType) {
        PyErr_Format(PyExc_RuntimeError, "libshiboken: Failed to get enum type %s", enumTypeName);
        return nullptr;
    }

    const char *dot = std::strrchr(fullName, '.');
    AutoDecRef name(Shiboken::String::fromCString(dot ? dot + 1 : fullName));
    AutoDecRef callArgs(Py_BuildValue("(OO)", name.object(), pyEnumItems));
    auto *newType = PyObject_Call(PyEnumType, callArgs, callDict);

    setModuleAndQualnameOnType(newType, fullName);

    return reinterpret_cast<PyTypeObject *>(newType);
}

} // namespace Shiboken::Enum
