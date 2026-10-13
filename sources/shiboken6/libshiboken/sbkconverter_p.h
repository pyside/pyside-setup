// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_CONVERTER_P_H
#define SBK_CONVERTER_P_H

#include "sbkpython.h"
#include "sbkconverter.h"
#include "sbkcppstring.h"
#include "sbkstring.h"
#include <limits>
#include <typeinfo>
#include <sstream>
#include <iostream>
#include <vector>
#ifdef Py_GIL_DISABLED
#  include "shibokenclasshelpermacros.h"
#  include <atomic>
#  include <cstddef>
#endif

extern "C"
{

using ToCppConversion = std::pair<IsConvertibleToCppFunc, PythonToCppFunc>;
using ToCppConversionVector = std::vector<ToCppConversion>;

#ifdef Py_GIL_DISABLED
} // extern "C"

// Read without a lock
class ToCppConversionList
{
public:
    struct Sentinel {};

    // Data and size as taken at begin(), like a vector's iterators
    class Iterator
    {
    public:
        Iterator(const ToCppConversionVector *list, std::size_t index)
            : m_data(list->data()), m_size(list->size()), m_index(index) {}
        const ToCppConversion &operator*() const { return m_data[m_index]; }
        Iterator &operator++() { ++m_index; return *this; }
        bool operator!=(Sentinel) const { return m_index < m_size; }
        std::size_t index() const { return m_index; }

    private:
        const ToCppConversion *m_data;
        std::size_t m_size;
        std::size_t m_index;
    };

    ToCppConversionList();
    ~ToCppConversionList();
    LIBSHIBOKEN_DISABLE_COPY(ToCppConversionList)

    Iterator begin() const { return {current(), 0}; }
    Iterator cbegin() const { return begin(); }
    Sentinel end() const { return {}; }
    bool empty() const { return current()->empty(); }
    std::size_t size() const { return current()->size(); }

    void clear();
    void emplace_back(const ToCppConversion &conversion);
    void insert(Iterator pos, const ToCppConversion &conversion);

private:
    const ToCppConversionVector *current() const
    {
        return m_current.load(std::memory_order_acquire);
    }
    void publish(ToCppConversionVector *next);

    std::atomic<const ToCppConversionVector *> m_current;
    std::vector<const ToCppConversionVector *> m_replaced;
};

extern "C"
{
#endif // Py_GIL_DISABLED

/*!
    \internal
    Private structure of SbkConverter.
*/
struct SbkConverter
{
    /*!
        Python type associated with this converter. If the type is a Shiboken
        wrapper, then it must be a SbkObjectType; otherwise it will be the
        Python type to which the C++ value will be converted (note that the
        C++ type could be produced from various Python types).
    */
    PyTypeObject *pythonType;
    /*!
        This function converts a C++ object to a Python object of the type
        indicated in pythonType. The identity of the C++ object is kept,
        because it looks for an already existing Python wrapper associated
        with the C++ instance.
        It is used to convert C++ pointers and references to Python objects.
    */
    CppToPythonFunc pointerToPython;
    /*!
        This function converts a C++ object to a Python object of the type
        indicated in pythonType. The identity of the object is not kept,
        because a new instance of the C++ object is created.
        It is used to convert objects passed by value, or reference, if said
        reference can't be traced to an object that already has a Python
        wrapper assigned for it.
    */
    CppToPythonFunc copyToPython;
    /*!
        Same as copyToPython, but additionally receives the \c{PyTypeObject *}.
        Both functions are checked.
    */
    // FIXME PYSIDE 7: Add PyTypeObject parameter to CppToPythonFunc?
    CppToPythonWithTypeFunc copyToPythonWithType;
    /*!
        This is a special case of a Python to C++ conversion. It returns
        the underlying C++ pointer of a Python wrapper passed as parameter
        or nullptr if the Python object is a None value.
        It comes separated from the other toCppConversions because if you
        have a Python object representing a Value Type the type checking
        for both \c ValueType and \c{ValueType *} would be the same, thus
        the first check would be true and the second would never be reached.
    */
    ToCppConversion toCppPointerConversion;
    /*!
        This is a list of type checking functions that return the
        proper Python to C++ conversion function, for the given Python
        object.
        For Object Types, that never have implicit conversions, this
        list is always empty.
    */
#ifdef Py_GIL_DISABLED
    ToCppConversionList toCppConversions;
#else
    ToCppConversionVector toCppConversions;
#endif
};

} // extern "C"

template<typename T, typename MaxLimitType, bool isSigned>
struct OverFlowCheckerBase {
    static void formatOverFlowMessage(const MaxLimitType &value,
                                      const std::string *valueAsString = nullptr)
    {
        std::ostringstream str;
        str << "libshiboken: Overflow: Value ";
        if (valueAsString != nullptr && !valueAsString->empty())
            str << *valueAsString;
        else
            str << value;
        str << " exceeds limits of type "
            << " [" << (isSigned ? "signed" : "unsigned")
            << "] \"" << typeid(T).name()
            << "\" (" << sizeof(T) << "bytes).";
        const std::string message = str.str();
        PyErr_WarnEx(PyExc_RuntimeWarning, message.c_str(), 0);
    }

    // Checks if an overflow occurred inside Python code.
    // Precondition: use after calls like PyLong_AsLongLong or PyLong_AsUnsignedLongLong.
    // Postcondition: if error ocurred, sets the string reference to the string representation of
    //                the passed value.
    static bool checkForInternalPyOverflow(PyObject *pyIn, std::string &valueAsString)
    {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyObject *stringRepresentation = PyObject_Str(pyIn);
            const char *cString = Shiboken::String::toCString(stringRepresentation);
            valueAsString.assign(cString);
            Py_DECREF(stringRepresentation);
            return true;
        }
        return false;
    }
};

// Helper template for checking if a value overflows when cast to type T.
// The MaxLimitType size is usually >= than type T size, so that it can still represent values that
// can't be stored in T (unless the types are of course the same).
// TLDR: MaxLimitType is either long long or unsigned long long.
template<typename T, typename MaxLimitType = PY_LONG_LONG,
                     bool isSigned = std::numeric_limits<T>::is_signed >
struct OverFlowChecker;

template<typename T, typename MaxLimitType>
struct OverFlowChecker<T, MaxLimitType, true> :
        public OverFlowCheckerBase<T, MaxLimitType, true> {
    static bool check(const MaxLimitType &value, PyObject *pyIn)
    {
        std::string valueAsString;
        const bool isOverflow =
            OverFlowChecker::checkForInternalPyOverflow(pyIn, valueAsString)
            || value < std::numeric_limits<T>::min()
            || value > std::numeric_limits<T>::max();
        if (isOverflow)
            OverFlowChecker::formatOverFlowMessage(value, &valueAsString);
        return isOverflow;
    }
};

template<typename T, typename MaxLimitType>
struct OverFlowChecker<T, MaxLimitType, false>
        : public OverFlowCheckerBase<T, MaxLimitType, false> {
    static bool check(const MaxLimitType &value, PyObject *pyIn)
    {
        std::string valueAsString;
        const bool isOverflow =
            OverFlowChecker::checkForInternalPyOverflow(pyIn, valueAsString)
            || value < 0
            || static_cast<unsigned long long>(value) > std::numeric_limits<T>::max();
        if (isOverflow)
            OverFlowChecker::formatOverFlowMessage(value, &valueAsString);
        return isOverflow;
    }
};
template<>
struct OverFlowChecker<PY_LONG_LONG, PY_LONG_LONG, true> :
        public OverFlowCheckerBase<PY_LONG_LONG, PY_LONG_LONG, true> {
    static bool check(const PY_LONG_LONG &value, PyObject *pyIn) {
        std::string valueAsString;
        const bool isOverflow = checkForInternalPyOverflow(pyIn, valueAsString);
        if (isOverflow)
            OverFlowChecker::formatOverFlowMessage(value, &valueAsString);
        return isOverflow;

    }
};
template<>
struct OverFlowChecker<double, PY_LONG_LONG, true> {
    static bool check(const double &, PyObject *) { return false; }
};
template<>
struct OverFlowChecker<float, PY_LONG_LONG, true> :
        public OverFlowCheckerBase<float, PY_LONG_LONG, true> {
    static bool check(const double &value, PyObject *)
    {
        const bool result = value < std::numeric_limits<float>::min()
                || value > std::numeric_limits<float>::max();
        if (result)
            formatOverFlowMessage(value);
        return result;
    }
};

// Basic primitive type converters ---------------------------------------------------------

template <typename T> struct Primitive {};

template <typename T>
struct OnePrimitive
{
    static PyObject *toPython(const void *) { return nullptr; }
    static PythonToCppFunc isConvertible(PyObject *) { return nullptr; }
    static void toCpp(PyObject *, void *) {}

    static SbkConverter *createConverter(PyTypeObject *type)
    {
        SbkConverter *converter = Shiboken::Conversions::createConverter(type,
                                                                         Primitive<T>::toPython);
        Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                             Primitive<T>::toCpp,
                                                             Primitive<T>::isConvertible);
        return converter;
    }
    static SbkConverter *createConverter()
    {
        return createConverter(Shiboken::SbkType<T>());
    }

};
template <typename T>
struct TwoPrimitive : OnePrimitive<T>
{
    static PythonToCppFunc isOtherConvertible(PyObject *) { return nullptr; }
    static void otherToCpp(PyObject *, void *) {}

    static SbkConverter *createConverter(PyTypeObject *type)
    {
        SbkConverter *converter = OnePrimitive<T>::createConverter(type);
        Shiboken::Conversions::addPythonToCppValueConversion(converter, Primitive<T>::otherToCpp, Primitive<T>::isOtherConvertible);
        return converter;
    }
    static SbkConverter *createConverter()
    {
        return createConverter(Shiboken::SbkType<T>());
    }

};

// Integers --------------------------------------------------------------------------------

template <typename INT>
struct IntPrimitive : TwoPrimitive<INT>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyLong_FromLong(*reinterpret_cast<const INT *>(cppIn));
    }
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        double result = PyFloat_AsDouble(pyIn);
        // If cast to long directly it could overflow silently.
        if (OverFlowChecker<INT>::check(result, pyIn))
            PyErr_SetObject(PyExc_OverflowError, nullptr);
        *reinterpret_cast<INT * >(cppOut) = static_cast<INT>(result);
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (PyFloat_Check(pyIn))
            return toCpp;
        return nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        PY_LONG_LONG result = PyLong_AsLongLong(pyIn);
        if (OverFlowChecker<INT>::check(result, pyIn))
            PyErr_SetObject(PyExc_OverflowError, nullptr);
        *reinterpret_cast<INT * >(cppOut) = static_cast<INT>(result);
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return otherToCpp;
        return nullptr;
    }
};
template <> struct Primitive<int> : IntPrimitive<int> {};
template <> struct Primitive<long> : IntPrimitive<long> {};
template <> struct Primitive<short> : IntPrimitive<short> {};
template <> struct Primitive<unsigned short> : IntPrimitive<unsigned short> {};

// Unsigned Long Integers ------------------------------------------------------------------

template <typename LONG>
struct UnsignedLongPrimitive : IntPrimitive<LONG>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyLong_FromUnsignedLong(*reinterpret_cast<const LONG *>(cppIn));
    }
};
template <> struct Primitive<unsigned int> : UnsignedLongPrimitive<unsigned int> {};
template <> struct Primitive<unsigned long> : UnsignedLongPrimitive<unsigned long> {};

// Big integers ----------------------------------------------------------------------------

template <>
struct Primitive<PY_LONG_LONG> : OnePrimitive<PY_LONG_LONG>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyLong_FromLongLong(*reinterpret_cast<const PY_LONG_LONG *>(cppIn));
    }
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        PY_LONG_LONG result = PyLong_AsLongLong(pyIn);
        if (OverFlowChecker<PY_LONG_LONG>::check(result, pyIn))
            PyErr_SetObject(PyExc_OverflowError, nullptr);
        *reinterpret_cast<PY_LONG_LONG * >(cppOut) = result;
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return toCpp;
        return nullptr;
    }
};

template <>
struct Primitive<unsigned PY_LONG_LONG> : OnePrimitive<unsigned PY_LONG_LONG>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyLong_FromUnsignedLongLong(*static_cast<const unsigned PY_LONG_LONG *>(cppIn));
    }
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        if (PyLong_Check(pyIn)) {
            unsigned PY_LONG_LONG result = PyLong_AsUnsignedLongLong(pyIn);
            if (OverFlowChecker<unsigned PY_LONG_LONG, unsigned PY_LONG_LONG>::check(result, pyIn))
                PyErr_SetObject(PyExc_OverflowError, nullptr);
            *reinterpret_cast<unsigned PY_LONG_LONG * >(cppOut) = result;
        }
        else {
            PyErr_SetString(PyExc_TypeError,
                            "libshiboken: Invalid type for unsigned long long conversion");
        }
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return toCpp;
        return nullptr;
    }
};

// Floating point --------------------------------------------------------------------------

template <typename FLOAT>
struct FloatPrimitive : TwoPrimitive<FLOAT>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyFloat_FromDouble(*reinterpret_cast<const FLOAT *>(cppIn));
    }
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        *reinterpret_cast<FLOAT *>(cppOut) = FLOAT(PyLong_AsDouble(pyIn));
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (PyLong_Check(pyIn) || PyLong_Check(pyIn))
            return toCpp;
        return nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        *reinterpret_cast<FLOAT *>(cppOut) = FLOAT(PyFloat_AsDouble(pyIn));
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return otherToCpp;
        return nullptr;
    }
};
template <> struct Primitive<float> : FloatPrimitive<float> {};
template <> struct Primitive<double> : FloatPrimitive<double> {};

// Boolean ---------------------------------------------------------------------------------

template <>
struct Primitive<bool> : OnePrimitive<bool>
{
    static PyObject *toPython(const void *cppIn)
    {
        return PyBool_FromLong(*reinterpret_cast<const bool *>(cppIn));
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return toCpp;
        return nullptr;
    }
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        *reinterpret_cast<bool *>(cppOut) = PyLong_AsLong(pyIn) != 0;
    }
};

// Characters ------------------------------------------------------------------------------

template <typename CHAR>
struct CharPrimitive : IntPrimitive<CHAR>
{
    static void toCpp(PyObject *pyIn, void *cppOut)
    {
        *reinterpret_cast<CHAR *>(cppOut) = CHAR(Shiboken::String::toCString(pyIn)[0]);
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (Shiboken::String::checkChar(pyIn))
            return toCpp;
        return nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        PY_LONG_LONG result = PyLong_AsLongLong(pyIn);
        if (OverFlowChecker<CHAR>::check(result, pyIn))
            PyErr_SetObject(PyExc_OverflowError, nullptr);
        *reinterpret_cast<CHAR *>(cppOut) = CHAR(result);
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        if (PyNumber_Check(pyIn))
            return otherToCpp;
        return nullptr;
    }
    static SbkConverter *createConverter()
    {
        SbkConverter *converter = IntPrimitive<CHAR>::createConverter();
        Shiboken::Conversions::addPythonToCppValueConversion(converter, CharPrimitive<CHAR>::otherToCpp, CharPrimitive<CHAR>::isOtherConvertible);
        return converter;
    }

};
template <> struct Primitive<signed char> : CharPrimitive<signed char> {};
template <> struct Primitive<unsigned char> : CharPrimitive<unsigned char> {};
template <> struct Primitive<char> : CharPrimitive<char> {
    using CharPrimitive<char>::toPython;
    static PyObject *toPython(const void *cppIn) {
        return Shiboken::String::fromCString(reinterpret_cast<const char *>(cppIn), 1);
    }
};



// Strings ---------------------------------------------------------------------------------

template <>
struct Primitive<const char *> : TwoPrimitive<const char *>
{
    static PyObject *toPython(const void *cppIn)
    {
        if (!cppIn)
            Py_RETURN_NONE;
        return Shiboken::String::fromCString(reinterpret_cast<const char *>(cppIn));
    }
    static void toCpp(PyObject *, void *cppOut)
    {
        *reinterpret_cast<const char **>(cppOut) = nullptr;
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (pyIn == Py_None)
            return toCpp;
        return nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        *reinterpret_cast<const char **>(cppOut) = Shiboken::String::toCString(pyIn);
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        if (Shiboken::String::check(pyIn))
            return otherToCpp;
        return nullptr;
    }
};

template <>
struct Primitive<std::string> : TwoPrimitive<std::string>
{
    static PyObject *toPython(const void *cppIn)
    {
        return Shiboken::String::fromCppString(*reinterpret_cast<const std::string *>(cppIn));
    }
    static void toCpp(PyObject *, void *cppOut)
    {
        reinterpret_cast<std::string *>(cppOut)->clear();
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (pyIn == Py_None)
            return toCpp;
        return nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        Shiboken::String::toCppString(pyIn, reinterpret_cast<std::string *>(cppOut));
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        if (Shiboken::String::check(pyIn))
            return otherToCpp;
        return nullptr;
    }
};

template <>
struct Primitive<std::wstring> : TwoPrimitive<std::wstring>
{
    static PyObject *toPython(const void *cppIn)
    {
        return Shiboken::String::fromCppWString(*reinterpret_cast<const std::wstring *>(cppIn));
    }
    static void toCpp(PyObject *, void *cppOut)
    {
        reinterpret_cast<std::wstring *>(cppOut)->clear();
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        return pyIn == Py_None ? toCpp : nullptr;
    }
    static void otherToCpp(PyObject *pyIn, void *cppOut)
    {
        Shiboken::String::toCppWString(pyIn, reinterpret_cast<std::wstring *>(cppOut));
    }
    static PythonToCppFunc isOtherConvertible(PyObject *pyIn)
    {
        return PyUnicode_Check(pyIn) ? otherToCpp : nullptr;
    }
};

// nullptr_t
template <>
struct Primitive<std::nullptr_t> : OnePrimitive<std::nullptr_t>
{
    static PyObject *toPython(const void * /* cppIn */)
    {
        Py_RETURN_NONE;
    }
    static void toCpp(PyObject *, void *cppOut)
    {
        *reinterpret_cast<std::nullptr_t *>(cppOut) = nullptr;
    }
    static PythonToCppFunc isConvertible(PyObject *pyIn)
    {
        if (pyIn == Py_None)
            return toCpp;
        return nullptr;
    }
};

namespace Shiboken::Conversions {

SbkConverter *createConverterObject(PyTypeObject *type,
                                    PythonToCppFunc toCppPointerConvFunc,
                                    IsConvertibleToCppFunc toCppPointerCheckFunc,
                                    CppToPythonFunc pointerToPythonFunc,
                                    CppToPythonFunc copyToPythonFunc);

LIBSHIBOKEN_API void dumpConverters();

/*!
    \internal
    Interface for sbkmodule which must reset the cache when a new module is loaded.
*/
LIBSHIBOKEN_API void clearNegativeLazyCache();

} // namespace Shiboken::Conversions

#endif // SBK_CONVERTER_P_H
