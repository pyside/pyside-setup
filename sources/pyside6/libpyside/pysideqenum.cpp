// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqenum.h"

#include <pysidelogging_p.h>

#include <autodecref.h>
#include <sbkconverter.h>
#include <sbkenum.h>
#include <sbkpep.h>
#include <sbkstaticstrings.h>
#include <sbkstring.h>
#ifdef Py_GIL_DISABLED
#  include <sbkftoptions.h>
#  include <sbkheldlocks.h>
#endif

#include <QtCore/qmetatype.h>
#include <QtCore/qdebug.h>
#include <QtCore/qlist.h>

#include <cstring>
#include <map>
#ifdef Py_GIL_DISABLED
#  include <mutex>
#  include <optional>
#endif
#include <utility>

///////////////////////////////////////////////////////////////
//
// PYSIDE-957: Create QEnum dynamically from Python Enum
//
//
extern "C" {

using namespace Shiboken;

static PyObject *analyzePyEnum(PyObject *pyenum)
{
    /*
     * This is the straight-forward implementation of QEnum/QFlag. It does no
     * longer create an equivalent Qt enum but takes the Python enum as-is.
     *
     * It parses an Enum/Flag derived Python enum completely so that
     * registering can be done without error checks. This would be impossible
     * in MetaObjectBuilderPrivate::parsePythonType.
     */
    AutoDecRef members(PyObject_GetAttr(pyenum, Shiboken::PyMagicName::members()));
    if (members.isNull())
        return nullptr;
    AutoDecRef items(PyMapping_Items(members));
    if (items.isNull())
        return nullptr;
    int iflag = PySide::QEnum::isFlag(pyenum);
    if (iflag < 0)
        return nullptr;
    Py_ssize_t nr_items = PySequence_Length(items);
    if (nr_items < 0)
        return nullptr;

    for (Py_ssize_t idx = 0; idx < nr_items; ++idx) {
        AutoDecRef item(PySequence_GetItem(items, idx));
        if (item.isNull())
            return nullptr;

        // The item should be a 2-element sequence of the key name and an
        // object containing the value.
        AutoDecRef key(PySequence_GetItem(item, 0));
        AutoDecRef member(PySequence_GetItem(item, 1));
        if (key.isNull() || member.isNull())
            return nullptr;
        if (!Shiboken::String::check(key)) {
            // '%.200s' is the safety stringbuffer size of most CPython functions.
            PyErr_Format(PyExc_TypeError,
                "QEnum expected a string mapping as __members__, got '%.200s'",
                Py_TYPE(key)->tp_name);
            return nullptr;
        }

        // Get the value.
        AutoDecRef value(PyObject_GetAttr(member, Shiboken::PyName::value()));
        if (value.isNull())
            return nullptr;
        if (!PyLong_Check(value)) {
            PyErr_Format(PyExc_TypeError,
                "QEnum expected an int value as '%.200s', got '%.200s'",
                Shiboken::String::toCString(key), Py_TYPE(value)->tp_name);
            return nullptr;
        }
    }
    Py_RETURN_NONE;
}

static Py_ssize_t get_lineno()
{
    auto *frame = reinterpret_cast<PyObject *>(PyEval_GetFrame());  // borrowed ref
    AutoDecRef ob_lineno(PyObject_GetAttr(frame, Shiboken::PyName::f_lineno()));
    if (ob_lineno.isNull() || !PyLong_Check(ob_lineno))
        return -1;
    return PyLong_AsSsize_t(ob_lineno);
}

static bool is_module_code()
{
    auto *frame = reinterpret_cast<PyObject *>(PyEval_GetFrame());  // borrowed ref
    AutoDecRef ob_code(PyObject_GetAttr(frame, Shiboken::PyName::f_code()));
    if (ob_code.isNull())
        return false;
    AutoDecRef ob_name(PyObject_GetAttr(ob_code, Shiboken::PyName::co_name()));
    if (ob_name.isNull())
        return false;
    const char *codename = Shiboken::String::toCString(ob_name);
    return std::strcmp(codename, "<module>") == 0;
}

} // extern "C"

// Helper code for dynamically creating QMetaType's for @QEnum

template <class UnderlyingInt>
static void defaultCtr(const QtPrivate::QMetaTypeInterface *, void *addr)
{
    auto *i = reinterpret_cast<UnderlyingInt *>(addr);
    *i = 0;
}

template <class UnderlyingInt>
static void debugOp(const QtPrivate::QMetaTypeInterface *mti, QDebug &debug, const void *addr)
{
    const auto value = *reinterpret_cast<const UnderlyingInt *>(addr);
    QDebugStateSaver saver(debug);
    debug << mti->name << '(';
    if constexpr (std::is_unsigned<UnderlyingInt>()) {
        debug << Qt::showbase << Qt::hex;
    } else {
        if (value >= 0)
            debug << Qt::showbase << Qt::hex;
    }
    debug << value << ')';
}

template <class UnderlyingInt>
QMetaType createEnumMetaTypeHelper(const QByteArray &name)
{
    auto *mti = new QtPrivate::QMetaTypeInterface {
        1,       // revision
        ushort(std::alignment_of<UnderlyingInt>()),
        sizeof(UnderlyingInt),
        uint(QMetaType::fromType<UnderlyingInt>().flags() | QMetaType::IsEnumeration),
        {},      // typeId
        nullptr, // metaObjectFn
        qstrdup(name.constData()),
        defaultCtr<UnderlyingInt>,
        nullptr, // copyCtr
        nullptr, // moveCtr
        nullptr, // dtor
        QtPrivate::QEqualityOperatorForType<UnderlyingInt>::equals,
        QtPrivate::QLessThanOperatorForType<UnderlyingInt>::lessThan,
        debugOp<UnderlyingInt>,
        nullptr, // dataStreamOut
        nullptr, // dataStreamIn
        nullptr  // legacyRegisterOp
    };

    QMetaType metaType(mti);

    metaType.id(); // enforce registration
    qCDebug(lcPySide, "libpyside: Registering @QEnum meta type \"%s\".", name.constData());
    return metaType;
}

/*!
    \namespace PySide::QEnum
    \brief Namespace for @QEnum.
    \inmodule libpyside
*/
namespace PySide::QEnum {

using EnumCollector = std::map<int, PyObject *>;

static EnumCollector &getEnumCollector()
{
#ifdef Py_GIL_DISABLED
    // Per thread: class body, QEnum decorators and metaclass call share a thread.
    static thread_local EnumCollector perThread;
    static EnumCollector shared;
    return Shiboken::FreeThreading::optionEnabled(
               Shiboken::FreeThreading::Option::QEnumCollectorPerThread) ? perThread : shared;
#else
    static EnumCollector result;
    return result;
#endif
}

int isFlag(PyObject *obType)
{
    /*
     * Find out if this is an Enum or a Flag derived class.
     * It checks also if things come from the enum module and if it is
     * an Enum or Flag class at all.
     *
     * The function is called in MetaObjectBuilderPrivate::parsePythonType
     * again to obtain the flag value.
     */
    int res = enumIsFlag(obType);
    if (res < 0) {
        auto *type = reinterpret_cast<PyTypeObject *>(obType);
        PyErr_Format(PyExc_TypeError, "type %.200s does not inherit from 'Enum' or 'Flag'",
                                      type->tp_name);
        return -1;
    }
    return bool(res);
}

PyObject *QEnumMacro(PyObject *pyenum, bool flag)
{
    /*
     * This is the official interface of 'QEnum'. It first calls 'analyzePyEnum'.
     * When called as toplevel enum, it simply returns after some checks.
     * Otherwise, 'pyenum' is stored for later use by the meta class registation.
     */
    int computedFlag = isFlag(pyenum);
    if (computedFlag < 0)
        return nullptr;
    if (bool(computedFlag) != flag) {
        AutoDecRef name(PyObject_GetAttr(pyenum, PyMagicName::qualname()));
        const auto *cname = String::toCString(name);
        const char *e = "Enum";
        const char *f = "Flag";
        PyErr_Format(PyExc_TypeError, "expected '%s' but got '%s' (%.200s)",
                                      flag ? f : e, flag ? e : f, cname);
        return nullptr;
    }
    auto *ok = analyzePyEnum(pyenum);
    if (ok == nullptr)
        return nullptr;
    if (is_module_code()) {
        // This is a toplevel enum which we resolve immediately.
        Py_INCREF(pyenum);
        return pyenum;
    }

    Py_ssize_t lineno = get_lineno();
    if (lineno < 0)
        return nullptr;
    // Handle the rest via line number and the meta class.
    Py_INCREF(pyenum);
    auto &enumCollector = getEnumCollector();
    if (auto it = enumCollector.find(lineno); it != enumCollector.end())
        Py_XDECREF(std::exchange(it->second, pyenum));
    else
        enumCollector.insert({lineno, pyenum});
    Py_RETURN_NONE;
}

std::vector<PyObject *> resolveDelayedQEnums(PyTypeObject *containerType)
{
    /*
     * This is the internal interface of 'QEnum'.
     * It is called at the end of the meta class call 'SbkObjectType_tp_new' via
     * MetaObjectBuilderPrivate::parsePythonType and resolves the collected
     * Python Enum arguments. The result is then registered.
     */
    auto &enumCollector = getEnumCollector();
    if (enumCollector.empty())
        return {};
    auto *obContainerType = reinterpret_cast<PyObject *>(containerType);
    Py_ssize_t lineno = get_lineno();

    std::vector<PyObject *> result;

    auto it = enumCollector.begin();
    while (it != enumCollector.end()) {
        int nr = it->first;
        PyObject *pyenum = it->second;
        if (nr >= lineno) {
            AutoDecRef name(PyObject_GetAttr(pyenum, PyMagicName::name()));
            if (name.isNull() || PyObject_SetAttr(obContainerType, name, pyenum) < 0)
                return {};
            result.push_back(pyenum);
            it = enumCollector.erase(it);
        } else {
            ++it;
        }
    }
    return result;
}

QByteArray getTypeName(PyTypeObject *type)
{
    if (!Shiboken::Enum::checkType(type))
        return {};

    Shiboken::AutoDecRef qualName(PyObject_GetAttr(reinterpret_cast<PyObject *>(type),
                                                   Shiboken::PyMagicName::qualname()));
    QByteArray result = Shiboken::String::toCString(qualName.object());
    result.replace(".", "::");

    const auto metaType = QMetaType::fromName(result);
    return metaType.isValid() && metaType.flags().testFlag(QMetaType::IsEnumeration)
        ? result : QByteArray{};
}

using GenericEnumType = int;
using GenericEnum64Type = unsigned long long;

struct GenericEnumRegistry
{
    QList<PyTypeObject *> enumTypes;
    QList<PyTypeObject *> enum64Types;
};

Q_GLOBAL_STATIC(GenericEnumRegistry, genericEnumTypeRegistry)

#ifdef Py_GIL_DISABLED
static std::mutex &genericEnumRegistryMutex()
{
    static std::mutex mutex;
    return mutex;
}

// QEnum classes in any thread append; every conversion of such an enum reads.
class GenericEnumRegistryGuard
{
public:
    Q_DISABLE_COPY_MOVE(GenericEnumRegistryGuard)

    GenericEnumRegistryGuard()
    {
        if (Shiboken::FreeThreading::optionEnabled(
                Shiboken::FreeThreading::Option::GenericEnumRegistryLock)) {
            m_guard.emplace(genericEnumRegistryMutex());
        }
    }

private:
    std::optional<Shiboken::TrackedGuard<std::mutex, Shiboken::RawLock::GenericEnumRegistry>>
        m_guard;
};
#endif // Py_GIL_DISABLED

} // namespace PySide::QEnum

template <class IntType>
static inline void genericEnumPythonToCppTpl(PyObject *pyIn, void *cppOut)
{
    const auto value = static_cast<IntType>(Shiboken::Enum::getValue(pyIn));
    *reinterpret_cast<IntType *>(cppOut) = value;
}

template <class IntType>
static inline PyObject *genericEnumCppToPythonTpl(PyTypeObject *pyType, const void *cppIn)
{
    const auto value = *reinterpret_cast<const IntType *>(cppIn);
    return Shiboken::Enum::newItem(pyType, value);
}

extern "C"
{

// int
static void genericEnumPythonToCpp(PyObject *pyIn, void *cppOut)
{
    genericEnumPythonToCppTpl<PySide::QEnum::GenericEnumType>(pyIn, cppOut);
}

static PythonToCppFunc isGenericEnumToCppConvertible(PyObject *pyIn)
{
#ifdef Py_GIL_DISABLED
    PySide::QEnum::GenericEnumRegistryGuard guard;
#endif

    if (PySide::QEnum::genericEnumTypeRegistry()->enumTypes.contains(Py_TYPE(pyIn)))
        return genericEnumPythonToCpp;
    return {};
}

static PyObject *genericEnumCppToPython(PyTypeObject *pyType, const void *cppIn)
{
    return genericEnumCppToPythonTpl<PySide::QEnum::GenericEnumType>(pyType, cppIn);
}

// unsigned long long
static void genericEnumPythonToCpp64(PyObject *pyIn, void *cppOut)
{
    genericEnumPythonToCppTpl<PySide::QEnum::GenericEnum64Type>(pyIn, cppOut);
}

static PythonToCppFunc isGenericEnumToCpp64Convertible(PyObject *pyIn)
{
#ifdef Py_GIL_DISABLED
    PySide::QEnum::GenericEnumRegistryGuard guard;
#endif

    if (PySide::QEnum::genericEnumTypeRegistry()->enum64Types.contains(Py_TYPE(pyIn)))
        return genericEnumPythonToCpp64;
    return {};
}

static PyObject *genericEnumCpp64ToPython(PyTypeObject *pyType, const void *cppIn)
{
    return genericEnumCppToPythonTpl<PySide::QEnum::GenericEnum64Type>(pyType, cppIn);
}

} // extern "C"

namespace PySide::QEnum
{

// int
QMetaType createGenericEnumMetaType(const QByteArray &name, PyTypeObject *pyType)
{
    SbkConverter *converter = Shiboken::Conversions::createConverter(pyType,
                                                                     genericEnumCppToPython);
    Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                         genericEnumPythonToCpp,
                                                         isGenericEnumToCppConvertible);
    Shiboken::Conversions::registerConverterName(converter, name.constData());
    Shiboken::Enum::setTypeConverter(pyType, converter, nullptr);

#ifdef Py_GIL_DISABLED
    {
        GenericEnumRegistryGuard guard;
        genericEnumTypeRegistry->enumTypes.append(pyType);
    }
#else
    genericEnumTypeRegistry->enumTypes.append(pyType);
#endif
    return createEnumMetaTypeHelper<GenericEnumType>(name);
}

// "unsigned long long"
QMetaType createGenericEnum64MetaType(const QByteArray &name, PyTypeObject *pyType)
{
    SbkConverter *converter = Shiboken::Conversions::createConverter(pyType,
                                                                     genericEnumCpp64ToPython);
    Shiboken::Conversions::addPythonToCppValueConversion(converter,
                                                         genericEnumPythonToCpp64,
                                                         isGenericEnumToCpp64Convertible);
    Shiboken::Conversions::registerConverterName(converter, name.constData());
    Shiboken::Enum::setTypeConverter(pyType, converter, nullptr);

#ifdef Py_GIL_DISABLED
    {
        GenericEnumRegistryGuard guard;
        genericEnumTypeRegistry()->enum64Types.append(pyType);
    }
#else
    genericEnumTypeRegistry()->enum64Types.append(pyType);
#endif
    return createEnumMetaTypeHelper<GenericEnum64Type>(name);
}

} // namespace PySide::QEnum

//
///////////////////////////////////////////////////////////////
