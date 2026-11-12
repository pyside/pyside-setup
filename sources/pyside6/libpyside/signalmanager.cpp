// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "signalmanager.h"
#include "signalmanager_p.h"
#include "pyobjectwrapper.h"
#include "pysideqobject_p.h"
#include "pysidesignal.h"
#include "pysidelogging_p.h"
#include "pysideproperty.h"
#include "pysideproperty_p.h"
#include "pyside_p.h"
#include "dynamicqmetaobject.h"
#include "pysidemetafunction_p.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <gilstate.h>
#include <pep384impl_p.h>
#include <sbkconverter.h>
#include <sbkenum.h>
#include <sbkpep.h>
#include <sbkpytuplerange.h>
#include <sbkstring.h>
#include <sbkstaticstrings.h>
#include <sbkerrors.h>

#include <QtCore/qbytearrayview.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qcoreevent.h>
#include <QtCore/qdebug.h>
#include <QtCore/qhash.h>
#include <QtCore/qmetatype.h>

#include <climits>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

#if QSLOT_CODE != 1 || QSIGNAL_CODE != 2
#error QSLOT_CODE and/or QSIGNAL_CODE changed! change the hardcoded stuff to the correct value!
#endif

PyObject *metaObjectAttr()
{
    static PyObject *const s = Shiboken::String::createStaticString("__METAOBJECT__");
    return s;
}

static void destroyMetaObject(PyObject *obj)
{
    void *ptr = PyCapsule_GetPointer(obj, nullptr);
    auto *meta = reinterpret_cast<PySide::MetaObjectBuilder *>(ptr);
    SbkObject *wrapper = Shiboken::BindingManager::instance().retrieveWrapper(meta);
    if (wrapper)
        Shiboken::BindingManager::instance().releaseWrapper(wrapper);
    delete meta;
}

static const char *metaCallName(QMetaObject::Call call)
{
    static const QHash<QMetaObject::Call, const char *> mapping = {
        {QMetaObject::InvokeMetaMethod, "InvokeMetaMethod"},
        {QMetaObject::ReadProperty, "ReadProperty"},
        {QMetaObject::WriteProperty, "WriteProperty"},
        {QMetaObject::ResetProperty, "ResetProperty"},
        {QMetaObject::CreateInstance, "CreateInstance"},
        {QMetaObject::IndexOfMethod, "IndexOfMethod"},
        {QMetaObject::RegisterPropertyMetaType, "RegisterPropertyMetaType"},
        {QMetaObject::RegisterMethodArgumentMetaType, "RegisterMethodArgumentMetaType"},
        {QMetaObject::BindableProperty, "BindableProperty"},
        {QMetaObject::CustomCall, "CustomCall"}
    };
    auto it = mapping.constFind(call);
    return it != mapping.constEnd() ? it.value() : "<Unknown>";
}

static QByteArray methodSignature(const QMetaMethod &method)
{
    QByteArray result;
    if (const auto *t = method.typeName()) {
        result += t;
        result += ' ';
    }
    result += method.methodSignature();
    return result;
}

static QByteArray msgCannotConvertParameter(const QByteArray &parameterTypeName,
                                            const QByteArray &signature, qsizetype p)
{
    return "Cannot call meta function \""_ba + signature
           + "\" because parameter " + QByteArray::number(p) + " of type \""_ba
           + parameterTypeName + "\" cannot be converted."_ba;
}

static inline QByteArray msgCannotConvertParameter(QMetaMethod method, qsizetype p)
{
    return msgCannotConvertParameter(method.parameterTypeName(p),
                                     methodSignature(method), p);
}

static QByteArray msgCannotConvertReturn(const QByteArray &signature)
{
    return "The return value of \""_ba + signature + "\" cannot be converted."_ba;
}

static inline QByteArray msgCannotConvertReturn(QMetaMethod method)
{
    return msgCannotConvertReturn(methodSignature(method));
}

struct SignalManagerPrivate
{
    static PySide::SignalManager::QmlMetaCallErrorHandler m_qmlMetaCallErrorHandler;

    static void handleMetaCallError(QObject *object, int *result);
    static int qtPropertyMetacall(QObject *object, QMetaObject::Call call,
                                  int id, void **args);
    static int qtPythonMetacall(QObject *object, const QMetaObject *metaObject,
                                const QMetaMethod &method, int id, void **args);
    static int qtSignalMetacall(QObject *object, const QMetaObject *metaObject,
                                const QMetaMethod &method, int id, void **args);
};

PySide::SignalManager::QmlMetaCallErrorHandler
    SignalManagerPrivate::m_qmlMetaCallErrorHandler = nullptr;

static PyObject *CopyCppToPythonPyObject(const void *cppIn)
{
    const auto *wrapper = reinterpret_cast<const PySide::PyObjectWrapper *>(cppIn);
    PyObject *pyOut = *wrapper;
    Py_XINCREF(pyOut);
    return pyOut;
}

/*!
    \namespace PySide::SignalManager
    \brief Namespace for signal functionality.
    \inmodule libpyside
*/
namespace PySide::SignalManager {

void init()
{
    // Force the metaObject attribute into existence. This fixes an
    // exit crash (Python 3.15/allocation asserting since GIL is not held)
    // when connections done in Qt C++ are disconnected by the destructor,
    // triggering disconnectNotify()/metaObject().
    // Note: SbkDeallocWrapperCommon() temporarily releases the GIL for
    // legacy bug 500 (~QPrintDialog hanging).
    [[maybe_unused]] auto *mo = metaObjectAttr();

    // Register Qt primitive typedefs used on signals.
    using namespace Shiboken;

    // Register PyObject type to use in queued signal and slot connections
    PyObjectWrapper::registerMetaType();

    // Register QVariant(enum) conversion to QVariant(int)
    QMetaType::registerConverter<PyObjectWrapper, int>(&PyObjectWrapper::toInt);

    // Register a shiboken converter for PyObjectWrapper->Python (value conversion).
    // Python->PyObjectWrapper is not registered since the converters do not work for
    // non-SbkObject types (falling back to plain pointer pass through).
    // This conversion needs to be done manually via QVariant.
    SbkConverter *converter = Shiboken::Conversions::createConverter(&PyBaseObject_Type,
                                                                     CopyCppToPythonPyObject);
    Shiboken::Conversions::registerConverterName(converter, "PyObject");
    Shiboken::Conversions::registerConverterName(converter, "object");
    Shiboken::Conversions::registerConverterName(converter, "PyObjectWrapper");
    Shiboken::Conversions::registerConverterName(converter, "PySide::PyObjectWrapper");
}

void setQmlMetaCallErrorHandler(QmlMetaCallErrorHandler handler)
{
    SignalManagerPrivate::m_qmlMetaCallErrorHandler = handler;
}

bool emitSignal(QObject *source, const char *signal, PyObject *args)
{
    if (!Signal::checkQtSignal(signal))
        return false;
    signal++;

    int signalIndex = source->metaObject()->indexOfSignal(signal);
    return emitSignal(source, signalIndex, args);
}

bool emitSignal(QObject* source, int signalIndex, PyObject* args)
{
    return signalIndex != -1 && MetaFunction::call(source, signalIndex, args);
}

void handleMetaCallError()
{
    const int reclimit = Py_GetRecursionLimit();
    // Inspired by Python's errors.c: PyErr_GivenExceptionMatches() function.
    // Temporarily bump the recursion limit, so that PyErr_Print will not raise a recursion
    // error again. Don't do it when the limit is already insanely high, to avoid overflow.
    if (reclimit < (1 << 30))
        Py_SetRecursionLimit(reclimit + 5);
    PyErr_Print();
    Py_SetRecursionLimit(reclimit);
}

// Special getAttr() function for function names from Qt's Meta Object system.
// It handles "private" methods prefixed '__' which are name-mangled by Python to
// '_ClassName__method' to hide them. In the normal case, getattr() receives
// the mangled name, but, when used from the Meta Object system (using connect()),
// QMetaMethod's name name will be unmangled and needs to be resolved.
// Try to find them by looping the MRO types (PYSIDE-772, PYSIDE-3376).
PyObject *methodGetAttr(PyObject *self, PyObject *name)
{
    PyObject *result = PyObject_GetAttr(self, name);
    if (result != nullptr || !_Pep_IsPrivateName(name))
        return result;

    auto *type = Py_TYPE(self);
    for (PyTypeObject *candidate : Shiboken::PyTupleTypeRange(type->tp_mro)) {
        if (candidate != &PyBaseObject_Type) {
            PyErr_Clear();
            Shiboken::AutoDecRef mangledName(_Pep_TypePrivateMangle(candidate, name));
            // _Pep_TypePrivateMangle can return nullptr on malloc or unicode failure.
            if (mangledName.isNull()) {
                PyErr_Clear();
                break;
            }
            result = PyObject_GetAttr(self, mangledName.object());
            if (result != nullptr)
                break;
        }
    }
    return result;
}

} // namespace PySide::SignalManager

// Handle errors from meta calls. Requires GIL and PyErr_Occurred()
void SignalManagerPrivate::handleMetaCallError(QObject *object, int *result)
{
    // Bubbles Python exceptions up to the Javascript engine, if called from one
    if (m_qmlMetaCallErrorHandler) {
        auto idOpt = m_qmlMetaCallErrorHandler(object);
        if (idOpt.has_value())
            *result = idOpt.value();
    }
    PySide::SignalManager::handleMetaCallError();
}

static const char *metaObjectCallName(QMetaObject::Call call)
{
    static const char *names[] = {
        "InvokeMetaMethod", "ReadProperty", "WriteProperty", "ResetProperty",
        "CreateInstance", "IndexOfMethod", "RegisterPropertyMetaType",
        "RegisterMethodArgumentMetaType", "BindableProperty", "CustomCall",
        "ConstructInPlace"};
    constexpr size_t count = sizeof(names)/sizeof(names[0]);
    static_assert(QMetaObject::ConstructInPlace == count - 1);
    return call >= 0 && call < count ? names[call] : "<unknown>";
}

// Handler for QMetaObject::ReadProperty/WriteProperty/ResetProperty:
int SignalManagerPrivate::qtPropertyMetacall(QObject *object,
                                             QMetaObject::Call call,
                                             int id, void **args)
{
    const QMetaObject *metaObject = object->metaObject();
    int result = id - metaObject->propertyCount();

    const QMetaProperty mp = metaObject->property(id);

    qCDebug(lcPySide).noquote().nospace() << __FUNCTION__
        << ' ' << metaCallName(call) << " #" << id << ' ' << mp.typeName()
        << "/\"" << mp.name() << "\" " << object;

    if (!mp.isValid())
        return result;

    Shiboken::GilState gil;
    auto *pySbkSelf = Shiboken::BindingManager::instance().retrieveWrapper(object);
    Q_ASSERT(pySbkSelf);
    auto *pySelf = reinterpret_cast<PyObject *>(pySbkSelf);
    Shiboken::AutoDecRef pp_name(Shiboken::String::fromCString(mp.name()));
    PySideProperty *pp = PySide::Property::getObject(pySelf, pp_name);
    if (!pp) {
        qWarning("libpyside: Invalid property: %s.", mp.name());
        return false;
    }
    pp->d->metaCall(pySelf, call, args);
    Py_DECREF(pp);
    if (PyErr_Occurred()) {
        // PYSIDE-2160: An unknown type was reported. Indicated by StopIteration.
        if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
            Shiboken::Errors::Stash errorStash;
            bool ign = call == QMetaObject::WriteProperty;
            PyErr_WarnFormat(PyExc_RuntimeWarning, 0,
                ign ? "libpyside: Unknown property type '%s' of QObject '%s' used in fset"
                    : "libpyside: Unknown property type '%s' of QObject '%s' used in fget with %R",
                pp->d->typeName().constData(), metaObject->className(), errorStash.getException());
            if (PyErr_Occurred())
                Shiboken::Errors::storeErrorOrPrint();
            errorStash.release();
            return result;
        }

        qWarning().noquote().nospace()
            << "libpyside: An error occurred executing the property metacall "
            << metaObjectCallName(call) << " on property \"" << mp.name()
            << "\" of " << PySide::debugQObject(object);
        handleMetaCallError(object, &result);
    }
    return result;
}

// Handler for QMetaObject::InvokeMetaMethod

static inline bool isSignalConnected(const QObject *object, const QMetaMethod &method)
{
    class FriendlyQObject : public QObject {
    public:
        using QObject::isSignalConnected; // protected
    };

    return static_cast<const FriendlyQObject *>(object)->isSignalConnected(method);
}

int SignalManagerPrivate::qtSignalMetacall(QObject *object, const QMetaObject *metaObject,
                                           const QMetaMethod &method, int id, void **args)
{
    qCDebug(lcPySide).noquote().nospace() << __FUNCTION__ << " #" << id
                                          << " \"" << method.methodSignature() << '"';

    int result = id - metaObject->methodCount();
    const bool isConnected = isSignalConnected(object, method);

    QMetaObject::activate(object, id, args); // emit python signal

    if (isConnected) { // Check for errors in connected Python slots.
        Shiboken::GilState gilState;
        if (PyErr_Occurred() != nullptr)
            handleMetaCallError(object, &result);
    }
    return result;
}

int SignalManagerPrivate::qtPythonMetacall(QObject *object, const QMetaObject *metaObject,
                                           const QMetaMethod &method, int id, void **args)
{
    qCDebug(lcPySide).noquote().nospace() << __FUNCTION__ << " #" << id
        << " \"" << method.methodSignature() << '"';

    Shiboken::GilState gil;
    auto *pySbkSelf = Shiboken::BindingManager::instance().retrieveWrapper(object);
    Q_ASSERT(pySbkSelf);
    auto *pySelf = reinterpret_cast<PyObject *>(pySbkSelf);
    Shiboken::AutoDecRef methodName(Shiboken::String::fromCString(method.name().constData()));
    Shiboken::AutoDecRef pyMethod(PySide::SignalManager::methodGetAttr(pySelf, methodName));
    if (pyMethod.isNull()) {
        PyErr_Format(PyExc_AttributeError, "Slot '%s::%s' not found.",
                     metaObject->className(), method.methodSignature().constData());
    } else {
        PySide::SignalManager::callPythonMetaMethod(method, args, pyMethod);
    }

    // WARNING Isn't safe to call any metaObject and/or object methods beyond this point
    //         because the object can be deleted inside the called slot.

    int result = id - metaObject->methodCount();
    if (PyErr_Occurred() != nullptr)
        handleMetaCallError(object, &result);

    return result;
}

namespace PySide::SignalManager {

int qt_metacall(QObject *object, QMetaObject::Call call, int id, void **args)
{
    switch (call) {
        case QMetaObject::ReadProperty:
        case QMetaObject::WriteProperty:
        case QMetaObject::ResetProperty:
            id = SignalManagerPrivate::qtPropertyMetacall(object, call, id, args);
            break;
        case QMetaObject::RegisterPropertyMetaType:
        case QMetaObject::BindableProperty:
            id -= object->metaObject()->propertyCount();
            break;
        case QMetaObject::InvokeMetaMethod: {
            const QMetaObject *metaObject = object->metaObject();
            const QMetaMethod method = metaObject->method(id);
            id = method.methodType() == QMetaMethod::Signal
                ? SignalManagerPrivate::qtSignalMetacall(object, metaObject, method, id, args)
                : SignalManagerPrivate::qtPythonMetacall(object, metaObject, method, id, args);
        }
            break;
        case QMetaObject::CreateInstance:
        case QMetaObject::IndexOfMethod:
        case QMetaObject::RegisterMethodArgumentMetaType:
        case QMetaObject::CustomCall:
            qCDebug(lcPySide).noquote().nospace() << __FUNCTION__ << ' '
                << metaCallName(call) << " #" << id << ' '  << object;
            id -= object->metaObject()->methodCount();
            break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        case QMetaObject::ConstructInPlace:
            break;
#endif
    }
    return id;
}

} // namespace PySide::SignalManager

// Helper for calling a Python pyCallable matching a Qt signal / slot.
enum CallResult : std::uint8_t
{
    CallOk,
    CallOtherError, // Python error set
    CallReturnValueError,
    CallArgumentError // Argument (return - CallArgumentError) caused an error
};

static inline bool isNonVoidReturn(const char *returnType)
{
    return returnType != nullptr && returnType[0] != 0 && std::strcmp("void", returnType) != 0;
}

static int callPythonMetaMethodHelper(const QByteArrayList &paramTypes,
                                      const char *returnType /* = nullptr */,
                                      void **args, PyObject *pyCallable)
{
    using SpecificConverter = Shiboken::Conversions::SpecificConverter;
    const qsizetype argsSize = paramTypes.size();
    Shiboken::AutoDecRef preparedArgs(PyTuple_New(argsSize));

    for (qsizetype i = 0; i < argsSize; ++i) {
        void *data = args[i + 1];
        const auto &param = paramTypes.at(i);
        SpecificConverter converter(param.constData());
        if (!converter.isValid())
            return CallResult::CallArgumentError + int(i);
        // Only pointer conversion available for const-ref - add indirection
        const bool valueToPtr = converter.conversionType() == SpecificConverter::PointerConversion
            && !param.endsWith('*') && param != "PyObject"_ba;
        auto *src = valueToPtr ? static_cast<void *>(&data) : data;
        PyTuple_SetItem(preparedArgs, i, converter.toPython(src));
    }

    std::optional<Shiboken::Conversions::SpecificConverter> retConverter;
    if (args[0] != nullptr && isNonVoidReturn(returnType)) {
        retConverter = Shiboken::Conversions::SpecificConverter(returnType);
        if (!retConverter->isValid())
            return CallResult::CallReturnValueError;
    }

    Shiboken::AutoDecRef retval(PyObject_CallObject(pyCallable, preparedArgs.object()));
    if (PyErr_Occurred() != nullptr || retval.isNull())
        return CallResult::CallOtherError;

    if (retval != Py_None && retConverter.has_value())
        retConverter.value().toCpp(retval, args[0]);
    return CallResult::CallOk;
}

static QByteArray signature(const char *name, const QByteArrayList &parameterTypes,
                            const char *returnType)
{
    QByteArray result;
    if (isNonVoidReturn(returnType))
        result += QByteArray(returnType) + ' ';
    result += QByteArray(name) + '(' + parameterTypes.join(", ") + ')';
    return result;
}

namespace PySide::SignalManager {

int callPythonMetaMethod(QMetaMethod method, void **args,
                                        PyObject *callable)
{
    Q_ASSERT(callable);

    Shiboken::GilState gil;
    int callResult = callPythonMetaMethodHelper(method.parameterTypes(),
                                                method.typeName(), args, callable);
    switch (callResult) {
    case CallOk:
        return 0;
    case CallOtherError:
        return -1;
    case CallReturnValueError:
        PyErr_SetString(PyExc_RuntimeError, msgCannotConvertReturn(method).constData());
        return -1;
    default: { // CallArgumentError + n
        const int arg = callResult - CallArgumentError;
        PyErr_SetString(PyExc_TypeError, msgCannotConvertParameter(method, arg).constData());
        return -1;
    }
    }
    return 0;
}

int callPythonMetaMethod(const QByteArrayList &parameterTypes,
                                        const char *returnType,
                                        void **args, PyObject *callable)
{
    Q_ASSERT(callable);

    Shiboken::GilState gil;
    int callResult = callPythonMetaMethodHelper(parameterTypes, returnType, args, callable);
    switch (callResult) {
    case CallOk:
        return 0;
    case CallOtherError:
        return -1;
    case CallReturnValueError: {
        const auto &sig = signature("slot", parameterTypes, returnType);
        PyErr_SetString(PyExc_RuntimeError, msgCannotConvertReturn(sig).constData());
        return -1;
    }
    default: { // CallArgumentError + n
        const int arg = callResult - CallArgumentError;
        const auto &sig = signature("slot", parameterTypes, returnType);
        const auto &msg = msgCannotConvertParameter(parameterTypes.at(arg), sig, arg);
        PyErr_SetString(PyExc_TypeError, msg.constData());
        return -1;
    }
    }
    return 0;
}

bool registerMetaMethod(QObject *source, const char *signature, QMetaMethod::MethodType type)
{
    int ret = registerMetaMethodGetIndex(source, signature, type);
    return (ret != -1);
}

} // namespace PySide::SignalManager

static PySide::MetaObjectBuilder *metaBuilderFromDict(PyObject *dict)
{
    // PYSIDE-803: The dict in this function is the ob_dict of an SbkObject.
    // The "metaObjectAttr" entry is only handled in this file. There is no
    // way in this function to involve the interpreter. Therefore, we need
    // no GIL.
    // Note that "SignalManager::registerMetaMethodGetIndex" has write actions
    // that might involve the interpreter, but in that context the GIL is held.
    if (!dict || !PyDict_Contains(dict, metaObjectAttr()))
        return nullptr;

    // PYSIDE-813: The above assumption is not true in debug mode:
    // PyDict_GetItem would touch PyThreadState_GET and the global error state.
    // PyDict_GetItemWithError instead can work without GIL.
    PyObject *pyBuilder = PyDict_GetItemWithError(dict, metaObjectAttr());
    return reinterpret_cast<PySide::MetaObjectBuilder *>(PyCapsule_GetPointer(pyBuilder, nullptr));
}

// Helper to format a method signature "foo(QString)" into
// Slot decorator "@Slot(str)"

struct slotSignature
{
    explicit slotSignature(const char *signature) : m_signature(signature) {}

    const char *m_signature;
};

QDebug operator<<(QDebug debug, const slotSignature &sig)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "@Slot(";
    QByteArrayView signature(sig.m_signature);
    const auto len = signature.size();
    auto pos = signature.indexOf('(');
    if (pos != -1 && pos < len - 2) {
        ++pos;
        while (true) {
            auto nextPos = signature.indexOf(',', pos);
            if (nextPos == -1)
                nextPos = len - 1;
            const QByteArrayView parameter = signature.sliced(pos, nextPos - pos);
            if (parameter == "QString") {
                debug << "str";
            } else if (parameter == "double") {
                debug << "float";
            } else {
                const bool hasDelimiter = parameter.contains("::");
                if (hasDelimiter)
                    debug << '"';
                if (!hasDelimiter && parameter.endsWith('*'))
                    debug << parameter.first(parameter.size() - 1);
                else
                    debug << parameter;
                if (hasDelimiter)
                    debug << '"';
            }
            pos = nextPos + 1;
            if (pos >= len)
                break;
            debug << ',';
        }
    }
    debug << ')';
    return debug;
}

static int addMetaMethod(QObject *source, const QByteArray &signature,
                         QMetaMethod::MethodType type)
{
    const QMetaObject *metaObject = source->metaObject();
    SbkObject *self = Shiboken::BindingManager::instance().retrieveWrapper(source);
    if (!Shiboken::Object::hasCppWrapper(self)) {
        qWarning().noquote().nospace() << "libpyside: " << __FUNCTION__
            << ": Cannot add dynamic method \"" << signature << "\" (" << type
            << ") to " << PySide::debugQObject(source) << ": No Wrapper found.";
        return -1;
    }

    auto *pySelf = reinterpret_cast<PyObject *>(self);
    auto *dict = SbkObject_GetDict_NoRef(pySelf);
    PySide::MetaObjectBuilder *dmo = metaBuilderFromDict(dict);
    // Create a instance meta object
    if (dmo == nullptr) {
        dmo = new PySide::MetaObjectBuilder(Py_TYPE(pySelf), metaObject);
        PyObject *pyDmo = PyCapsule_New(dmo, nullptr, destroyMetaObject);
        PyObject_SetAttr(pySelf, metaObjectAttr(), pyDmo);
        Py_DECREF(pyDmo);
    }

    if (type == QMetaMethod::Slot) {
        qCWarning(lcPySide).noquote().nospace()
            << "libpyside: Warning: Registering dynamic slot \""
            << signature << "\" on " << PySide::debugQObject(source)
            << ". Consider annotating with " << slotSignature(signature);
    }

    return type == QMetaMethod::Signal ? dmo->addSignal(signature) : dmo->addSlot(signature);
}

static inline void warnNullSource(const char *signature)
{
    qWarning("libpyside: SignalManager::registerMetaMethodGetIndex(\"%s\") called with source=nullptr.",
             signature);
}

namespace PySide::SignalManager {

int registerMetaMethodGetIndex(QObject *source, const char *signature,
                               QMetaMethod::MethodType type)
{
    if (source == nullptr) {
        warnNullSource(signature);
        return -1;
    }
    const QMetaObject *metaObject = source->metaObject();
    const int methodIndex = metaObject->indexOfMethod(signature);
    // Create the dynamic signal if needed
    return methodIndex != -1
        ? methodIndex : addMetaMethod(source, QByteArray(signature), type);
}

int registerMetaMethodGetIndexBA(QObject* source, const QByteArray &signature,
                                 QMetaMethod::MethodType type)
{
    if (source == nullptr) {
        warnNullSource(signature.constData());
        return -1;
    }
    const QMetaObject *metaObject = source->metaObject();
    const int methodIndex = metaObject->indexOfMethod(signature.constData());
    // Create the dynamic signal if needed
    return methodIndex != -1
        ? methodIndex : addMetaMethod(source, signature, type);
}

const QMetaObject *retrieveMetaObject(const QObject *cppSelf)
{
    SbkObject *pySelf = Shiboken::BindingManager::instance().retrieveWrapper(cppSelf);
    return pySelf != nullptr ? retrieveMetaObject(reinterpret_cast<PyObject *>(pySelf)) : nullptr;
}

const QMetaObject *retrieveMetaObject(PyObject *self)
{
#ifdef Py_GIL_DISABLED
    // PYSIDE-2221: When working with disable-gil, it seems to be necessary
    //              to hold the GIL. Maybe that is harmless here (check later).
    // Thanks to Sam Gross who fixed most errors by pointing this out.
    Shiboken::GilState gil;
#endif
    // PYSIDE-803: Avoid the GIL in SignalManager::retrieveMetaObject
    // This function had the GIL. We do not use the GIL unless we have to.
    // metaBuilderFromDict accesses a Python dict, but in that context there
    // is no way to reach the interpreter, see "metaBuilderFromDict".
    //
    // The update function is MetaObjectBuilderPrivate::update in
    // dynamicmetaobject.c . That function now uses the GIL when the
    // m_dirty flag is set.
    Q_ASSERT(self);

    auto *ob_dict = SbkObject_GetDict_NoRef(self);
    MetaObjectBuilder *builder = metaBuilderFromDict(ob_dict);
    if (!builder)
        builder = &(retrieveTypeUserData(self)->mo);

    return builder->update();
}

} // namespace PySide::SignalManager
