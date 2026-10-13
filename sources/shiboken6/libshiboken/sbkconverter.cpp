// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkconverter.h"
#include "sbkconverter_p.h"
#include "sbkarrayconverter_p.h"
#include "sbkmodule_p.h"
#include "basewrapper_p.h"
#include "bindingmanager.h"
#include "autodecref.h"
#include "helper.h"
#include "sbkstring.h"
#include "sbkpep.h"
#include "sbkpydictrange.h"
#include "voidptr.h"

#include <string>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>

static SbkConverter **PrimitiveTypeConverters;

using ConvertersMap = std::unordered_map<std::string, SbkConverter *>;
static ConvertersMap converters;

bool SbkObject_TypeCheck(PyTypeObject *tp, PyObject *ob)
{
    return Py_TYPE(ob) == tp || PyType_IsSubtype(Py_TYPE(ob), tp);
}

/*!
    \namespace Shiboken::Conversions

    \brief Namespace for converter-related functionality.

    \inmodule libshiboken
*/
namespace Shiboken::Conversions {

void initArrayConverters();

void init()
{
    static SbkConverter *primitiveTypeConverters[] = {
        Primitive<PY_LONG_LONG>::createConverter(),
        Primitive<bool>::createConverter(),
        Primitive<char>::createConverter(),
        Primitive<const char *>::createConverter(&PyUnicode_Type),
        Primitive<double>::createConverter(),
        Primitive<float>::createConverter(),
        Primitive<int>::createConverter(),
        Primitive<long>::createConverter(),
        Primitive<short>::createConverter(),
        Primitive<signed char>::createConverter(),
        Primitive<std::string>::createConverter(&PyUnicode_Type),
        Primitive<std::wstring>::createConverter(&PyUnicode_Type),
        Primitive<unsigned PY_LONG_LONG>::createConverter(),
        Primitive<unsigned char>::createConverter(),
        Primitive<unsigned int>::createConverter(),
        Primitive<unsigned long>::createConverter(),
        Primitive<unsigned short>::createConverter(),
        VoidPtr::createConverter(),
        Primitive<std::nullptr_t>::createConverter()
    };
    PrimitiveTypeConverters = primitiveTypeConverters;

    assert(converters.empty());
    converters["PY_LONG_LONG"] = primitiveTypeConverters[SBK_PY_LONG_LONG_IDX];
    converters["bool"] = primitiveTypeConverters[SBK_BOOL_IDX_1];
    converters["char"] = primitiveTypeConverters[SBK_CHAR_IDX];
    converters["const char *"] = primitiveTypeConverters[SBK_CONSTCHARPTR_IDX];
    converters["double"] = primitiveTypeConverters[SBK_DOUBLE_IDX];
    converters["float"] = primitiveTypeConverters[SBK_FLOAT_IDX];
    converters["int"] = primitiveTypeConverters[SBK_INT_IDX];
    converters["long"] = primitiveTypeConverters[SBK_LONG_IDX];
    converters["short"] = primitiveTypeConverters[SBK_SHORT_IDX];
    converters["signed char"] = primitiveTypeConverters[SBK_SIGNEDCHAR_IDX];
    converters["std::string"] = primitiveTypeConverters[SBK_STD_STRING_IDX];
    converters["std::wstring"] = primitiveTypeConverters[SBK_STD_WSTRING_IDX];
    converters["unsigned PY_LONG_LONG"] = primitiveTypeConverters[SBK_UNSIGNEDPY_LONG_LONG_IDX];
    converters["unsigned char"] = primitiveTypeConverters[SBK_UNSIGNEDCHAR_IDX];
    converters["unsigned int"] = primitiveTypeConverters[SBK_UNSIGNEDINT_IDX];
    converters["unsigned long"] = primitiveTypeConverters[SBK_UNSIGNEDLONG_IDX];
    converters["unsigned short"] = primitiveTypeConverters[SBK_UNSIGNEDSHORT_IDX];
    converters["void*"] = primitiveTypeConverters[SBK_VOIDPTR_IDX];
    converters["std::nullptr_t"] = primitiveTypeConverters[SBK_NULLPTR_T_IDX];

    initArrayConverters();
}

static void dumpPyTypeObject(std::ostream &str, PyTypeObject *t)
{
    str << "\nPython type ";
    if (t == nullptr) {
        str << "<None>";
        return;
    }
    str << '"' << PepType_GetFullyQualifiedNameStr(t) << '"';
    if (t->tp_base != nullptr && t->tp_base != &PyBaseObject_Type)
        str << '(' << PepType_GetFullyQualifiedNameStr(t->tp_base) << ')';
}

static void dumpSbkConverter(std::ostream &str, const SbkConverter *c)
{
    str << "SbkConverter " << static_cast<const void *>(c) << ": ";
    if (c->pointerToPython != nullptr)
        str << ", C++ pointer->Python";
    if (c->copyToPython != nullptr)
        str << ", copy->Python";
    if (c->toCppPointerConversion.second != nullptr)
        str << ", Python->C++ pointer";
    if (!c->toCppConversions.empty())
        str << ", " << c->toCppConversions.size() << " Python->C++ conversions";
}

// Less than operator for a PyTypeObject for dumping the converter map
static bool pyTypeObjectLessThan(const PyTypeObject *t1, const PyTypeObject *t2)
{
    const bool isNull1 = t1 == nullptr;
    const bool isNull2 = t2 == nullptr;
    if (isNull1 || isNull2)
        return isNull1 && !isNull2;
    // Internal types (lower case) first
    const bool isInternal1 = std::islower(t1->tp_name[0]);
    const bool isInternal2 = std::islower(t2->tp_name[0]);
    if (isInternal1 != isInternal2)
        return !isInternal2;
    return std::strcmp(t1->tp_name, t2->tp_name) < 0;
}

void dumpConverters()
{
    struct PyTypeObjectLess {

        bool operator()(const PyTypeObject *t1, const PyTypeObject *t2) const {
            return pyTypeObjectLessThan(t1, t2);
        }
    };

    using StringSet = std::set<std::string>;
    using SbkConverterNamesMap = std::unordered_map<SbkConverter *, StringSet>;
    using PyTypeObjectConverterMap = std::map<PyTypeObject *, SbkConverterNamesMap,
                                              PyTypeObjectLess>;

    auto &str = std::cerr;

    // Sort the entries by the associated PyTypeObjects and converters
    PyTypeObjectConverterMap pyTypeObjectConverterMap;
    for (const auto &converter : converters) {
        auto *sbkConverter = converter.second;
        if (sbkConverter == nullptr) {
            str << "Non-existent: \"" << converter.first << "\"\n";
            continue;
        }
        auto *typeObject = sbkConverter->pythonType;
        auto typeIt = pyTypeObjectConverterMap.find(typeObject);
        if (typeIt == pyTypeObjectConverterMap.end())
            typeIt = pyTypeObjectConverterMap.insert(std::make_pair(typeObject,
                                                                    SbkConverterNamesMap{})).first;
        SbkConverterNamesMap &sbkConverterMap = typeIt->second;
        auto convIt = sbkConverterMap.find(sbkConverter);
        if (convIt == sbkConverterMap.end())
            convIt = sbkConverterMap.insert(std::make_pair(sbkConverter,
                                                           StringSet{})).first;
        convIt->second.insert(converter.first);
    }

     for (const auto &tc : pyTypeObjectConverterMap) {
         dumpPyTypeObject(str, tc.first);
         str << ", " << tc.second.size() << " converter(s):\n";
         for (const auto &cn : tc.second) {
             str << "    ";
             dumpSbkConverter(str, cn.first);
             str << ", " << cn.second.size() << " alias(es):";
             int i = 0;
             for (const auto &name : cn.second) {
                 if ((i++ % 5) == 0)
                     str << "\n        ";
                 str << " \"" << name << '"';
             }
             str << '\n';
         }
    }

    str << '\n';
}

SbkConverter *createConverterObject(PyTypeObject *type,
                                           PythonToCppFunc toCppPointerConvFunc,
                                           IsConvertibleToCppFunc toCppPointerCheckFunc,
                                           CppToPythonFunc pointerToPythonFunc,
                                           CppToPythonFunc copyToPythonFunc)
{
    auto *converter = new SbkConverter;
    converter->pythonType = type;
    // PYSIDE-595: Provide reference for heaptypes
    if (type != nullptr && (type->tp_flags & Py_TPFLAGS_HEAPTYPE) != 0)
        Py_INCREF(reinterpret_cast<PyObject *>(type));

    converter->pointerToPython = pointerToPythonFunc;
    converter->copyToPython = copyToPythonFunc;
    converter->copyToPythonWithType = nullptr;

    if (toCppPointerCheckFunc && toCppPointerConvFunc)
        converter->toCppPointerConversion = std::make_pair(toCppPointerCheckFunc, toCppPointerConvFunc);
    converter->toCppConversions.clear();

    return converter;
}

/*!
    Creates a converter for a wrapper type.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a type
            \li A Shiboken.ObjectType that will receive the new converter.
        \row
            \li \a toCppPointerConvFunc
            \li Function to retrieve the C++ pointer held by a Python wrapper.
        \row
            \li \a toCppPointerCheckFunc
            \li Check and return the retriever function of the C++ pointer
                held by a Python wrapper.
        \row
            \li \a pointerToPythonFunc
            \li Function to convert a C++ object to a Python \a type wrapper,
                keeping their identity.
        \row
            \li \a copyToPythonFunc
            \li Function to convert a C++ object to a Python \a type,
                copying the object.
        \row
            \li \e Return
            \li The new converter referred by the wrapper \a type.
    \endtable
*/
SbkConverter *createConverter(PyTypeObject *type,
                              PythonToCppFunc toCppPointerConvFunc,
                              IsConvertibleToCppFunc toCppPointerCheckFunc,
                              CppToPythonFunc pointerToPythonFunc,
                              CppToPythonFunc copyToPythonFunc)
{
    SbkConverter *converter =
        createConverterObject(type,
                              toCppPointerConvFunc, toCppPointerCheckFunc,
                              pointerToPythonFunc, copyToPythonFunc);
    PepType_SOTP(type)->converter = converter;
    return converter;
}

SbkConverter *createConverter(PyTypeObject *type, CppToPythonFunc toPythonFunc)
{
    return createConverterObject(type, nullptr, nullptr, nullptr, toPythonFunc);
}

SbkConverter *createConverter(PyTypeObject *type, CppToPythonWithTypeFunc toPythonFunc)
{
    auto *result = createConverterObject(type, nullptr, nullptr, nullptr, nullptr);
    result->copyToPythonWithType = toPythonFunc;
    return result;
}

void deleteConverter(SbkConverter *converter)
{
    if (converter) {
        converter->toCppConversions.clear();
        delete converter;
    }
}

void setCppPointerToPythonFunction(SbkConverter *converter, CppToPythonFunc pointerToPythonFunc)
{
    converter->pointerToPython = pointerToPythonFunc;
}

void setPythonToCppPointerFunctions(SbkConverter *converter,
                                    PythonToCppFunc toCppPointerConvFunc,
                                    IsConvertibleToCppFunc toCppPointerCheckFunc)
{
    converter->toCppPointerConversion = std::make_pair(toCppPointerCheckFunc, toCppPointerConvFunc);
}

void addPythonToCppValueConversion(SbkConverter *converter,
                                   PythonToCppFunc pythonToCppFunc,
                                   IsConvertibleToCppFunc isConvertibleToCppFunc)
{
    converter->toCppConversions.emplace_back(std::make_pair(isConvertibleToCppFunc, pythonToCppFunc));
}

void prependPythonToCppValueConversion(SbkConverter *converter,
                                       PythonToCppFunc pythonToCppFunc,
                                       IsConvertibleToCppFunc isConvertibleToCppFunc)
{
    converter->toCppConversions.insert(converter->toCppConversions.begin(),
                                       std::make_pair(isConvertibleToCppFunc, pythonToCppFunc));
}

void addPythonToCppValueConversion(PyTypeObject *type,
                                   PythonToCppFunc pythonToCppFunc,
                                   IsConvertibleToCppFunc isConvertibleToCppFunc)
{
    auto *sotp = PepType_SOTP(type);
    addPythonToCppValueConversion(sotp->converter, pythonToCppFunc, isConvertibleToCppFunc);
}

void addPythonToCppValueConversion(Shiboken::Module::TypeInitStruct typeStruct,
                                   PythonToCppFunc pythonToCppFunc,
                                   IsConvertibleToCppFunc isConvertibleToCppFunc)
{
    addPythonToCppValueConversion(typeStruct.type, pythonToCppFunc, isConvertibleToCppFunc);
}

PyObject *pointerToPython(PyTypeObject *type, const void *cppIn)
{
    auto *sotp = PepType_SOTP(type);
    return pointerToPython(sotp->converter, cppIn);
}

PyObject *pointerToPython(const SbkConverter *converter, const void *cppIn)
{
    assert(converter);
    if (!cppIn)
        Py_RETURN_NONE;
    if (!converter->pointerToPython) {
        warning(PyExc_RuntimeWarning, 0, "pointerToPython(): SbkConverter::pointerToPython is null for \"%s\".",
                converter->pythonType->tp_name);
        Py_RETURN_NONE;
    }
    return converter->pointerToPython(cppIn);
}

PyObject *referenceToPython(PyTypeObject *type, const void *cppIn)
{
    auto *sotp = PepType_SOTP(type);
    return referenceToPython(sotp->converter, cppIn);
}

PyObject *referenceToPython(const SbkConverter *converter, const void *cppIn)
{
    assert(cppIn);

    if (auto sbkOut = BindingManager::instance().wrapperRef(cppIn, converter->pythonType))
        return sbkOut.newRef();
    if (!converter->pointerToPython) {
        warning(PyExc_RuntimeWarning, 0, "referenceToPython(): SbkConverter::pointerToPython is null for \"%s\".",
                converter->pythonType->tp_name);
        Py_RETURN_NONE;
    }
    return converter->pointerToPython(cppIn);
}

static inline PyObject *CopyCppToPython(const SbkConverter *converter, const void *cppIn)
{
    if (!cppIn)
        Py_RETURN_NONE;
    if (converter->copyToPythonWithType != nullptr)
        return converter->copyToPythonWithType(converter->pythonType, cppIn);
    if (converter->copyToPython != nullptr)
        return converter->copyToPython(cppIn);
    warning(PyExc_RuntimeWarning, 0, "CopyCppToPython(): SbkConverter::copyToPython is null for \"%s\".",
            converter->pythonType->tp_name);
    Py_RETURN_NONE;
}

PyObject *copyToPython(PyTypeObject *type, const void *cppIn)
{
    auto *sotp = PepType_SOTP(type);
    return CopyCppToPython(sotp->converter, cppIn);
}

PyObject *copyToPython(const SbkConverter *converter, const void *cppIn)
{
    return CopyCppToPython(converter, cppIn);
}

PythonToCppFunc isPythonToCppPointerConvertible(PyTypeObject *type, PyObject *pyIn)
{
    assert(pyIn);
    auto *sotp = PepType_SOTP(type);
    return sotp->converter->toCppPointerConversion.first(pyIn);
}

PythonToCppConversion pythonToCppPointerConversion(PyTypeObject *type, PyObject *pyIn)
{
    if (pyIn == nullptr)
        return {};
    if (PythonToCppFunc toCppPtr = isPythonToCppPointerConvertible(type, pyIn))
        return {toCppPtr, PythonToCppConversion::Pointer};
    return {};
}

PythonToCppConversion pythonToCppPointerConversion(Module::TypeInitStruct typeStruct, PyObject *pyIn)
{
    return pythonToCppPointerConversion(typeStruct.type, pyIn);
}

static inline PythonToCppFunc IsPythonToCppConvertible(const SbkConverter *converter, PyObject *pyIn)
{
    assert(pyIn);
    for (const ToCppConversion &c : converter->toCppConversions) {
        if (PythonToCppFunc toCppFunc = c.first(pyIn))
            return toCppFunc;
    }
    return nullptr;
}

PythonToCppFunc isPythonToCppValueConvertible(PyTypeObject *type, PyObject *pyIn)
{
    auto *sotp = PepType_SOTP(type);
    return IsPythonToCppConvertible(sotp->converter, pyIn);
}

PythonToCppConversion pythonToCppValueConversion(PyTypeObject *type, PyObject *pyIn)
{
    if (pyIn == nullptr)
        return {};
    if (PythonToCppFunc toCppVal = isPythonToCppValueConvertible(type, pyIn))
        return {toCppVal, PythonToCppConversion::Value};
    return {};
}

PythonToCppFunc isPythonToCppConvertible(const SbkConverter *converter, PyObject *pyIn)
{
    return IsPythonToCppConvertible(converter, pyIn);
}

PythonToCppConversion pythonToCppReferenceConversion(const SbkConverter *converter, PyObject *pyIn)
{
    if (converter->toCppPointerConversion.first) {
        if (auto toCppPtr = converter->toCppPointerConversion.first(pyIn))
            return {toCppPtr, PythonToCppConversion::Pointer};
    }
    for (const ToCppConversion &c : converter->toCppConversions) {
        if (PythonToCppFunc toCppFunc = c.first(pyIn))
            return {toCppFunc, PythonToCppConversion::Value};
    }
    return {};
}

PythonToCppConversion pythonToCppConversion(const SbkConverter *converter, PyObject *pyIn)
{
    if (auto func = IsPythonToCppConvertible(converter, pyIn))
        return {func, PythonToCppConversion::Value};
    return {};
}

PythonToCppFunc isPythonToCppConvertible(const SbkArrayConverter *converter,
                                         int dim1, int dim2, PyObject *pyIn)
{
    assert(pyIn);
    for (IsArrayConvertibleToCppFunc f : converter->toCppConversions) {
        if (PythonToCppFunc c = f(pyIn, dim1, dim2))
            return c;
    }
    return nullptr;
}

LIBSHIBOKEN_API PythonToCppConversion pythonToCppConversion(const SbkArrayConverter *converter,
                                                                int dim1, int dim2, PyObject *pyIn)
{
    if (auto func = isPythonToCppConvertible(converter, dim1, dim2, pyIn))
        return {func, PythonToCppConversion::Value};
    return {};
}

PythonToCppFunc isPythonToCppReferenceConvertible(PyTypeObject *type, PyObject *pyIn)
{
    if (pyIn != Py_None) {
        PythonToCppFunc toCpp = isPythonToCppPointerConvertible(type, pyIn);
        if (toCpp)
            return toCpp;
    }
    return isPythonToCppValueConvertible(type, pyIn);
}

PythonToCppConversion pythonToCppReferenceConversion(PyTypeObject *type, PyObject *pyIn)
{
    if (pyIn == nullptr)
        return {};
    if (pyIn != Py_None) {
        if (PythonToCppFunc toCppPtr = isPythonToCppPointerConvertible(type, pyIn))
            return {toCppPtr, PythonToCppConversion::Pointer};
    }
    if (PythonToCppFunc toCppVal = isPythonToCppValueConvertible(type, pyIn))
        return {toCppVal, PythonToCppConversion::Value};
    return {};
}

void nonePythonToCppNullPtr(PyObject *, void *cppOut)
{
    assert(cppOut);
    *static_cast<void **>(cppOut) = nullptr;
}

void *cppPointer(PyTypeObject *desiredType, SbkObject *pyIn)
{
    assert(pyIn);
    if (!ObjectType::checkType(desiredType)) {
        std::cerr << __FUNCTION__ << ": Conversion to non SbkObject type "
                  << PepType_GetFullyQualifiedNameStr(desiredType)
                  << " requested, falling back to pass-through.\n";
        return pyIn;
    }
    auto *inType = Shiboken::pyType(pyIn);
    if (ObjectType::hasCast(inType))
        return ObjectType::cast(inType, pyIn, desiredType);
    return Object::cppPointer(pyIn, desiredType);
}

void pythonToCppPointer(PyTypeObject *type, PyObject *pyIn, void *cppOut)
{
    assert(type);
    assert(pyIn);
    assert(cppOut);
    *reinterpret_cast<void **>(cppOut) = pyIn == Py_None
        ? nullptr
        : cppPointer(type, reinterpret_cast<SbkObject *>(pyIn));
}

void pythonToCppPointer(const SbkConverter *converter, PyObject *pyIn, void *cppOut)
{
    assert(converter);
    assert(pyIn);
    assert(cppOut);
    *reinterpret_cast<void **>(cppOut) = pyIn == Py_None
        ? nullptr
        : cppPointer(converter->pythonType, reinterpret_cast<SbkObject *>(pyIn));
}

static void _pythonToCppCopy(const SbkConverter *converter, PyObject *pyIn, void *cppOut)
{
    assert(converter);
    assert(pyIn);
    assert(cppOut);
    PythonToCppFunc toCpp = IsPythonToCppConvertible(converter, pyIn);
    if (toCpp) {
        toCpp(pyIn, cppOut);
    } else {
        std::cerr << __FUNCTION__ << ": Cannot copy-convert " << pyIn;
        if (pyIn)
            std::cerr << " (" << PepType_GetFullyQualifiedNameStr(Py_TYPE(pyIn)) << ')';
        std::cerr << " to C++.\n";
    }
}

void pythonToCppCopy(PyTypeObject *type, PyObject *pyIn, void *cppOut)
{
    assert(type);
    auto *sotp = PepType_SOTP(type);
    _pythonToCppCopy(sotp->converter, pyIn, cppOut);
}

void pythonToCppCopy(const SbkConverter *converter, PyObject *pyIn, void *cppOut)
{
    _pythonToCppCopy(converter, pyIn, cppOut);
}

bool isImplicitConversion(PyTypeObject *type, PythonToCppFunc toCppFunc)
{
    auto *sotp = PepType_SOTP(type);
    // This is the Object Type or Value Type conversion that only
    // retrieves the C++ pointer held in the Python wrapper.
    if (toCppFunc == sotp->converter->toCppPointerConversion.second)
        return false;

    // Object Types doesn't have any kind of value conversion,
    // only C++ pointer retrieval.
    if (sotp->converter->toCppConversions.empty())
        return false;

    // The first conversion of the non-pointer conversion list is
    // a Value Type's copy to C++ function, which is not an implicit
    // conversion.
    // Otherwise it must be one of the implicit conversions.
    // Note that we don't check if the Python to C++ conversion is in
    // the list of the type's conversions, for it is expected that the
    // caller knows what he's doing.
    const auto conv = sotp->converter->toCppConversions.cbegin();
    return toCppFunc != (*conv).second;
}

void registerConverterName(SbkConverter *converter, const char *typeName)
{
    auto iter = converters.find(typeName);
    if (iter == converters.end())
        converters.insert(std::make_pair(typeName, converter));
    else
        iter->second = converter;
}

void registerConverterAlias(SbkConverter *converter, const char *typeName)
{
    auto iter = converters.find(typeName);
    if (iter == converters.end())
        converters.insert(std::make_pair(typeName, converter));
}

static std::string getRealTypeName(const std::string &typeName)
{
    auto size = typeName.size();
    if (std::isalnum(typeName[size - 1]) == 0)
        return typeName.substr(0, size - 1);
    return typeName;
}

// PYSIDE-2404: Build a negative cache of already failed lookups.
//              The resulting list must be reset after each new import,
//              because that can change results. Also clear the cache after
//              reaching some threashold.
using NonExistingTypeNames = std::unordered_set<std::string>;

static NonExistingTypeNames &getNonExistingTypeNames()
{
    static NonExistingTypeNames result;
    return result;
}

// Arbitrary size limit to prevent random name overflows.
static constexpr std::size_t negativeCacheLimit = 50;

static void rememberAsNonexistent(const std::string &typeName)
{
    auto &nonExistingTypeNames = getNonExistingTypeNames();
    if (nonExistingTypeNames.size() > negativeCacheLimit)
        clearNegativeLazyCache();
    converters.insert(std::make_pair(typeName, nullptr));
    nonExistingTypeNames.insert(typeName);
}

SbkConverter *getConverter(const char *typeNameC)
{
    std::string typeName = typeNameC;
    auto it = converters.find(typeName);
    // PYSIDE-2404: This can also contain explicit nullptr as a negative cache.
    if (it != converters.end())
        return it->second;
    // PYSIDE-2404: Did not find the name. Load the lazy classes
    //              which have this name and try again.
    Shiboken::Module::loadLazyClassesWithNameStd(getRealTypeName(typeName));
    it = converters.find(typeName);
    if (it != converters.end())
        return it->second;
    // Cache the negative result. Don't forget to clear the cache for new modules.
    rememberAsNonexistent(typeName);

    if (Shiboken::pyVerbose() > 0) {
        const std::string message =
            std::string("Can't find type resolver for type '") + typeName + "'.";
        PyErr_WarnEx(PyExc_RuntimeWarning, message.c_str(), 0);
    }
    return nullptr;
}

void clearNegativeLazyCache()
{
    auto &nonExistingTypeNames = getNonExistingTypeNames();
    for (const auto &typeName : nonExistingTypeNames) {
        auto it = converters.find(typeName);
        converters.erase(it);
    }
    nonExistingTypeNames.clear();
}

SbkConverter *primitiveTypeConverter(int index)
{
    return PrimitiveTypeConverters[index];
}

bool checkIterableTypes(PyTypeObject *type, PyObject *pyIn)
{
    Shiboken::AutoDecRef it(PyObject_GetIter(pyIn));
    if (it.isNull()) {
        PyErr_Clear();
        return false;
    }

    while (true) {
        Shiboken::AutoDecRef pyItem(PyIter_Next(it.object()));
        if (pyItem.isNull()) {
            if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration))
                PyErr_Clear();
            break;
        }
        if (!PyObject_TypeCheck(pyItem, type))
            return false;
    }
    return true;
}

bool checkSequenceTypes(PyTypeObject *type, PyObject *pyIn)
{
    assert(type);
    assert(pyIn);
    if (PySequence_Size(pyIn) < 0) {
        // clear the error if < 0 which means no length at all
        PyErr_Clear();
        return false;
    }
    const Py_ssize_t size = PySequence_Size(pyIn);
    for (Py_ssize_t i = 0; i < size; ++i) {
        if (!PyObject_TypeCheck(AutoDecRef(PySequence_GetItem(pyIn, i)), type))
            return false;
    }
    return true;
}

bool convertibleIterableTypes(const SbkConverter *converter, PyObject *pyIn)
{
    Shiboken::AutoDecRef it(PyObject_GetIter(pyIn));
    if (it.isNull()) {
        PyErr_Clear();
        return false;
    }

    while (true) {
        Shiboken::AutoDecRef pyItem(PyIter_Next(it.object()));
        if (pyItem.isNull()) {
            if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration))
                PyErr_Clear();
            break;
        }
        if (!isPythonToCppConvertible(converter, pyItem))
            return false;
    }
    return true;
}

bool convertibleSequenceTypes(const SbkConverter *converter, PyObject *pyIn)
{
    assert(converter);
    assert(pyIn);
    if (!PySequence_Check(pyIn))
        return false;
    const Py_ssize_t size = PySequence_Size(pyIn);
    for (Py_ssize_t i = 0; i < size; ++i) {
        if (!isPythonToCppConvertible(converter, AutoDecRef(PySequence_GetItem(pyIn, i))))
            return false;
    }
    return true;
}
bool convertibleSequenceTypes(PyTypeObject *type, PyObject *pyIn)
{
    assert(type);
    auto *sotp = PepType_SOTP(type);
    return convertibleSequenceTypes(sotp->converter, pyIn);
}

bool convertibleIterableTypes(PyTypeObject *type, PyObject *pyIn)
{
    assert(type);
    auto *sotp = PepType_SOTP(type);
    return convertibleIterableTypes(sotp->converter, pyIn);
}

bool checkPairTypes(PyTypeObject *firstType, PyTypeObject *secondType, PyObject *pyIn)
{
    assert(firstType);
    assert(secondType);
    assert(pyIn);
    if (!PySequence_Check(pyIn))
        return false;
    if (PySequence_Size(pyIn) != 2)
        return false;
    if (!PyObject_TypeCheck(AutoDecRef(PySequence_GetItem(pyIn, 0)), firstType))
        return false;
    if (!PyObject_TypeCheck(AutoDecRef(PySequence_GetItem(pyIn, 1)), secondType))
        return false;
    return true;
}
bool convertiblePairTypes(const SbkConverter *firstConverter, bool firstCheckExact,
                          const SbkConverter *secondConverter, bool secondCheckExact,
                          PyObject *pyIn)
{
    assert(firstConverter);
    assert(secondConverter);
    assert(pyIn);
    if (!PySequence_Check(pyIn))
        return false;
    if (PySequence_Size(pyIn) != 2)
        return false;
    AutoDecRef firstItem(PySequence_GetItem(pyIn, 0));
    if (firstCheckExact) {
        if (!PyObject_TypeCheck(firstItem, firstConverter->pythonType))
            return false;
    } else if (!isPythonToCppConvertible(firstConverter, firstItem)) {
        return false;
    }
    AutoDecRef secondItem(PySequence_GetItem(pyIn, 1));
    if (secondCheckExact) {
        if (!PyObject_TypeCheck(secondItem, secondConverter->pythonType))
            return false;
    } else if (!isPythonToCppConvertible(secondConverter, secondItem)) {
        return false;
    }

    return true;
}

bool checkDictTypes(PyTypeObject *keyType, PyTypeObject *valueType, PyObject *pyIn)
{
    assert(keyType);
    assert(valueType);
    assert(pyIn);
    if (!PyDict_Check(pyIn))
        return false;

    for (auto [key, value] : PyDictRange(pyIn)) {
        if (!PyObject_TypeCheck(key, keyType))
            return false;
        if (!PyObject_TypeCheck(value, valueType))
            return false;
    }
    return true;
}

bool checkMultiDictTypes(PyTypeObject *keyType, PyTypeObject *valueType,
                         PyObject *pyIn)
{
    assert(keyType);
    assert(valueType);
    assert(pyIn);
    if (!PyDict_Check(pyIn))
        return false;

    for (auto [key, values] : PyDictRange(pyIn)) {
        if (!PyObject_TypeCheck(key, keyType))
            return false;
        if (!PySequence_Check(values))
            return false;
        const Py_ssize_t size = PySequence_Size(values);
        for (Py_ssize_t i = 0; i < size; ++i) {
            AutoDecRef value(PySequence_GetItem(values, i));
            if (!PyObject_TypeCheck(value, valueType))
                return false;
        }
    }
    return true;
}

bool convertibleDictTypes(const SbkConverter *keyConverter, bool keyCheckExact, const SbkConverter *valueConverter,
                          bool valueCheckExact, PyObject *pyIn)
{
    assert(keyConverter);
    assert(valueConverter);
    assert(pyIn);
    if (!PyDict_Check(pyIn))
        return false;
    for (auto [key, value] : PyDictRange(pyIn)) {
        if (keyCheckExact) {
            if (!PyObject_TypeCheck(key, keyConverter->pythonType))
                return false;
        } else if (!isPythonToCppConvertible(keyConverter, key)) {
            return false;
        }
        if (valueCheckExact) {
            if (!PyObject_TypeCheck(value, valueConverter->pythonType))
                return false;
        } else if (!isPythonToCppConvertible(valueConverter, value)) {
            return false;
        }
    }
    return true;
}

bool convertibleMultiDictTypes(const SbkConverter *keyConverter, bool keyCheckExact,
                               const SbkConverter *valueConverter,
                               bool valueCheckExact, PyObject *pyIn)
{
    assert(keyConverter);
    assert(valueConverter);
    assert(pyIn);
    if (!PyDict_Check(pyIn))
        return false;
    for (auto [key, values] : PyDictRange(pyIn)) {
        if (keyCheckExact) {
            if (!PyObject_TypeCheck(key, keyConverter->pythonType))
                return false;
        } else if (!isPythonToCppConvertible(keyConverter, key)) {
            return false;
        }
        if (!PySequence_Check(values))
            return false;
        const Py_ssize_t size = PySequence_Size(values);
        for (Py_ssize_t i = 0; i < size; ++i) {
            AutoDecRef value(PySequence_GetItem(values, i));
            if (valueCheckExact) {
                if (!PyObject_TypeCheck(value.object(), valueConverter->pythonType))
                    return false;
            } else if (!isPythonToCppConvertible(valueConverter, value.object())) {
                return false;
            }
        }
    }
    return true;
}

PyTypeObject *getPythonTypeObject(const SbkConverter *converter)
{
    if (converter)
        return converter->pythonType;
    return nullptr;
}

PyTypeObject *getPythonTypeObject(const char *typeName)
{
    return getPythonTypeObject(getConverter(typeName));
}

static bool hasCopyToPythonFunc(const SbkConverter *converter)
{
    return converter->copyToPython != nullptr || converter->copyToPythonWithType != nullptr;
}

bool pythonTypeIsValueType(const SbkConverter *converter)
{
    // Unlikely to happen but for multi-inheritance SbkObjs
    // the converter is not defined, hence we need a default return.
    if (!converter)
        return false;
    return converter->pointerToPython && hasCopyToPythonFunc(converter);
}

bool pythonTypeIsObjectType(const SbkConverter *converter)
{
    return converter->pointerToPython && !hasCopyToPythonFunc(converter);
}

bool pythonTypeIsWrapperType(const SbkConverter *converter)
{
    return converter->pointerToPython != nullptr;
}

/*!
    \class Shiboken::Conversions::SpecificConverter

    \brief C++ Wrapper around a type converter, which is constructed from a name.

    \inmodule libshiboken
*/
SpecificConverter::SpecificConverter(const char *typeName)
    : m_converter(getConverter(typeName)), m_type(InvalidConversion)
{
    if (!m_converter)
        return;
    const auto len = std::strlen(typeName);
    char lastChar = typeName[len -1];
    if (lastChar == '&') {
        m_type = ReferenceConversion;
    } else if (lastChar == '*' || pythonTypeIsObjectType(m_converter)) {
        m_type = PointerConversion;
    } else {
        m_type = CopyConversion;
    }
}

PyObject *SpecificConverter::toPython(const void *cppIn) const
{
    switch (m_type) {
    case CopyConversion:
        return copyToPython(m_converter, cppIn);
    case PointerConversion:
        return pointerToPython(m_converter, *static_cast<const void * const *>(cppIn));
    case ReferenceConversion:
        return referenceToPython(m_converter, cppIn);
    default:
        PyErr_SetString(PyExc_RuntimeError,
                        "libshiboken: tried to use invalid converter in 'C++ to Python' conversion");
    }
    return nullptr;
}

void SpecificConverter::toCpp(PyObject *pyIn, void *cppOut) const
{
    switch (m_type) {
    case CopyConversion:
        pythonToCppCopy(m_converter, pyIn, cppOut);
        break;
    case PointerConversion:
        pythonToCppPointer(m_converter, pyIn, cppOut);
        break;
    case ReferenceConversion:
        pythonToCppPointer(m_converter, pyIn, static_cast<void*>(&cppOut));
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError,
                        "libshiboken: tried to use invalid converter in 'Python to C++' conversion");
    }
}

} // namespace Shiboken::Conversions

bool SbkChar_Check(PyObject *X)
{
    return PyNumber_Check(X) || Shiboken::String::checkChar(X);
}
