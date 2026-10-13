// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pyside.h"
#include "pysideinit.h"
#include "pysidecleanup.h"
#include "pysidemetatype.h"
#include "pysideqapp.h"
#include "pysideqobject.h"
#include "pysideqobject_p.h"
#include "pysideutils.h"
#include "pyside_p.h"
#include "signalmanager_p.h"
#include "pysideclassinfo_p.h"
#include "pysideglobals_p.h"
#include "pysideproperty_p.h"
#include "class_property.h"
#include "pysideproperty.h"
#include "pysidesignal.h"
#include "pysidesignal_p.h"
#include "pysidestaticstrings.h"
#include "pysideslot_p.h"
#include "pysidemetafunction_p.h"
#include "pysidemetafunction.h"
#include "dynamicqmetaobject.h"
#include "feature_select.h"
#include "pysidelogging_p.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <gilstate.h>
#include <helper.h>
#include <pep384ext.h>
#include <sbkconverter.h>
#include <sbkerrors.h>
#include <sbkpep.h>
#include <sbkstring.h>
#include <sbkstaticstrings.h>
#include <sbkfeature_base.h>
#include <sbkmodule.h>
#include <sbkpydictrange.h>
#include <sbkpytuplerange.h>
#ifdef Py_GIL_DISABLED
#  include <sbkfailpoint.h>
#  include <sbkftoptions.h>
#  include <QtCore/qpointer.h>
#  include <QtCore/qtimer.h>
#endif

#include <QtCore/qabstracteventdispatcher.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qhash.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qmutex.h>
#include <QtCore/qstack.h>
#include <QtCore/qthread.h>
#include <QtCore/private/qobject_p.h>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <memory>
#include <optional>
#include <typeinfo>

#ifdef Q_OS_WIN
#  include <conio.h>
#else
#  include <QtCore/qdeadlinetimer.h>
#  include <QtCore/private/qcore_unix_p.h>
#endif

using namespace Qt::StringLiterals;

static QStack<PySide::CleanupFunction> cleanupFunctionList;

// Used by QML (main thread), but needs to be protected against other
// threads constructing QObject's.
static void thread_local *qobjectNextAddr;

QT_BEGIN_NAMESPACE
extern bool qRegisterResourceData(int, const unsigned char *, const unsigned char *,
        const unsigned char *);
QT_END_NAMESPACE

Q_LOGGING_CATEGORY(lcPySide, "qt.pyside.libpyside", QtCriticalMsg)

static QObjectData *qt_object_private(const QObject *o)
{
    class FriendlyQObject : public QObject {
    public:
        using QObject::d_ptr;
    };
    return static_cast<const FriendlyQObject *>(o)->d_ptr.data();
}

static bool hasDynamicMetaObject(const QObject *o)
{
    return qt_object_private(o)->metaObject != nullptr;
}

/*!
    \module libpyside
    \title PySide utility library
    \brief Contains functionality for Python bindings of Qt objects.
*/

/*!
    \namespace PySide
    \brief Root namespace.
    \inmodule libpyside
*/
namespace PySide
{

void init(PyObject *module)
{
    qobjectNextAddr = nullptr;
    ClassInfo::init(module);
    Signal::init(module);
    Slot::init(module);
    Property::init(module);
    ClassProperty::init(module);
    MetaFunction::init(module);
    // Init signal manager, so it will register some meta types used by QVariant.
    SignalManager::init();
    initQApp();
}

static QByteArray _sigWithMangledName(const QByteArray &signature, bool mangle)
{
    if (!mangle)
        return signature;
    auto bracePos = signature.indexOf('(');
    auto limit = bracePos >= 0 ? bracePos : signature.size();
    if (limit < 3)
        return signature;
    QByteArray result;
    result.reserve(signature.size() + 4);
    for (auto i = 0; i < limit; ++i) {
        const char c = signature.at(i);
        if (std::isupper(c)) {
            if (i > 0) {
                if (std::isupper(signature.at(i - 1)))
                    return signature; // Give up at consecutive upper chars
                 result.append('_');
            }
            result.append(std::tolower(c));
        } else {
            result.append(c);
        }
    }
    // Copy the rest after the opening brace (if any)
    result.append(signature.mid(limit));
    return result;
}

static QByteArray _sigWithOrigName(const QByteArray &signature, bool mangle)
{
    if (!mangle)
        return signature;
    auto bracePos = signature.indexOf('(');
    auto limit = bracePos >= 0 ? bracePos : signature.size();
    QByteArray result;
    result.reserve(signature.size());
    for (auto i = 0; i < limit; ++i) {
        const char c = signature.at(i);
        if (std::isupper(c)) {
            if (i > 0) {
                if (std::isupper(signature.at(i - 1)))
                    return signature;       // Give up at consecutive upper chars
                return QByteArray{};        // Error, this was not converted!
            }
        }
        if (std::islower(c) && i > 0 && signature.at(i - 1) == '_') {
            result.chop(1);
            result.append(std::toupper(c));
        } else {
            result.append(c);
        }
    }
    // Copy the rest after the opening brace (if any)
    result.append(signature.mid(limit));
    return result;
}

/*****************************************************************************
 *
 * How do we find a property?
 * --------------------------
 *
 * There are methods which are truly parts of properties, and there are
 * other property-like methods which are not. True properties can be
 * found by inspecting `SbkObjectType_GetPropertyStrings(type)`.
 *
 * Pseudo-properties have only a getter and a setter, and we must assume that
 * the name of the getter is the property name, and the name of the setter
 * is the uppercase of the getter with "set" prepended.
 *
 * We first walk the mro and search the property name and get the setter
 * name. If that doesn't work, we use the heuristics for the setter.
 * We then do the final mro lookup.
 *
 * Note that the true property lists have the original names, while the
 * dict entries in the mro are already mangled.
 */

static QByteArrayList parseFields(const char *propStr, int flags, bool *stdWrite)
{
    /*
     * Break the string into subfields at ':' and add defaults.
     */
    if (stdWrite)
        *stdWrite = true;
    QByteArray s = QByteArray(propStr);
    auto list = s.split(':');
    assert(list.size() == 2 || list.size() == 3);
    auto name = list[0];
    auto read = list[1];
    if (read.isEmpty())
        list[1] = name;
    if (list.size() == 2)
        return list;
    auto write = list[2];
    if (stdWrite)
        *stdWrite = write.isEmpty();
    if (write.isEmpty()) {
        auto snake_flag = flags & 0x01;
        if (snake_flag) {
            list[2] = ("set_") + name;
        } else {
            list[2] = QByteArray("set") + name;
            list[2][3] = std::toupper(list[2][3]);
        }
    }
    return list;
}

static QByteArrayList _SbkType_LookupProperty(PyTypeObject *type,
                                              const QByteArray &name, int flags)
{
    /*
     * Looks up a property and returns all fields.
     */
    int snake_flag = flags & 0x01;
    QByteArray origName(_sigWithOrigName(name, snake_flag));
    if (origName.isEmpty())
        return QByteArrayList{};
    auto len = std::strlen(origName);
    for (PyTypeObject *base : Shiboken::PyTupleTypeRange(type->tp_mro)) {
        if (!SbkObjectType_Check(base))
            continue;
        auto *props = SbkObjectType_GetPropertyStrings(base);
        if (props == nullptr || *props == nullptr)
            continue;
        for (; *props != nullptr; ++props) {
            QByteArray propStr(*props);
            if (std::strncmp(propStr, origName, len) == 0) {
                if (propStr[len] != ':')
                    continue;
                // We found the property. Return the parsed fields.
                propStr = _sigWithMangledName(propStr, snake_flag);
                return parseFields(propStr, flags, nullptr);
            }
        }
    }
    return QByteArrayList{};
}

static QByteArrayList _SbkType_FakeProperty(const QByteArray &name, int flags)
{
    /*
     * Handle a pseudo.property and return all fields.
     */
    int snake_flag = flags & 0x01;
    QByteArray propStr(name);
    propStr += "::";
    propStr = _sigWithMangledName(propStr, snake_flag);
    return parseFields(propStr, snake_flag, nullptr);
}

static bool _setProperty(PyObject *qObj, PyObject *name, PyObject *value, bool *accept)
{
    using Shiboken::AutoDecRef;

    QByteArray propName(Shiboken::String::toCString(name));
    auto *type = Py_TYPE(qObj);
    int flags = currentSelectId(type);
    int prop_flag = flags & 0x02;
    auto found = false;
    QByteArray getterName;
    QByteArray setterName;

    auto fields = _SbkType_LookupProperty(type, propName, flags);
    if (!fields.isEmpty()) {
        found = true;
        bool haveWrite = fields.size() == 3;
        if (!haveWrite)
            return false;
    } else {
        fields = _SbkType_FakeProperty(propName, flags);
    }

    propName = fields[0];
    getterName = fields[1];
    setterName = fields[2];

    // PYSIDE-1702: We do not use getattr, since that could trigger an action
    //              if we have a true property. Better to look inside the mro.
    //              That should return a descriptor or a property.
    PyObject *look{};

    if (found && prop_flag) {
        // We have a property, and true_property is active.
        // There must be a property object and we use it's fset.
        AutoDecRef pyPropName(Shiboken::String::fromCString(propName.constData()));
        look = _PepType_Lookup(Py_TYPE(qObj), pyPropName);
    } else {
        // We have a pseudo property or true_property is off, looking for a setter.
        AutoDecRef pySetterName(Shiboken::String::fromCString(setterName.constData()));
        look = _PepType_Lookup(Py_TYPE(qObj), pySetterName);
    }

    if (look) {
        AutoDecRef propSetter{};
        static PyObject *magicGet = Shiboken::PyMagicName::get();
        if (found && prop_flag) {
            // the indirection of the setter descriptor in a true property
            AutoDecRef descr(PyObject_GetAttr(look, PySideName::fset()));
            propSetter.reset(PyObject_CallMethodObjArgs(descr, magicGet, qObj, nullptr));
        } else {
            // look is already the descriptor
            propSetter.reset(PyObject_CallMethodObjArgs(look, magicGet, qObj, nullptr));
        }
        *accept = true;
        AutoDecRef args(PyTuple_Pack(1, value));
        AutoDecRef retval(PyObject_CallObject(propSetter, args));
        if (retval.isNull())
            return false;
    } else {
        PyErr_Clear();
        AutoDecRef attr(PyObject_GenericGetAttr(qObj, name));
        if (PySide::Property::checkType(attr)) {
            *accept = true;
            if (PySide::Property::setValue(reinterpret_cast<PySideProperty *>(
                    attr.object()), qObj, value) < 0)
                return false;
        }
    }
    return true;
}

// PYSIDE-2329: Search a signal by name (Note: QMetaObject::indexOfSignal()
// searches by signature).
static std::optional<QMetaMethod> findSignal(const QMetaObject *mo,
                                             const QByteArray &name)
{
    const auto count = mo->methodCount();
    for (int i = mo->methodOffset(); i < count; ++i) {
        const auto method = mo->method(i);
        if (method.methodType() == QMetaMethod::Signal && method.name() == name)
            return method;
    }
    const auto *base = mo->superClass();
    return base != nullptr ? findSignal(base, name) : std::nullopt;
}

/*!
    Fills QObject properties and does signal connections using the values
    found in the \a kwds dictionary.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a qObj
            \li PyObject for the QObject.
        \row
            \li \a metaObj
            \li QMetaObject of \a qObj.
        \row
            \li \a kwds
            \li Key/value dictionary.
        \row
            \li \a allowErrors
            \li If true, keys that are neither properties nor signals
                are ignored.
    \endtable

    Returns true if everything goes well, false with a Python error set
    otherwise.
*/
bool fillQtProperties(PyObject *qObj, const QMetaObject *metaObj,
                      PyObject *kwds, bool allowErrors)
{
    int flags = currentSelectId(Py_TYPE(qObj));
    int snake_flag = flags & 0x01;

    for (auto [key, value] : Shiboken::PyDictRange(kwds)) {
        const QByteArray propName = Shiboken::String::toCString(key);
        QByteArray unmangledName = _sigWithOrigName(propName, snake_flag);
        bool accept = false;
        // PYSIDE-1705: Make sure that un-mangled names are not recognized in snake_case mode.
        if (!unmangledName.isEmpty()) {
            if (metaObj->indexOfProperty(unmangledName) != -1) {
                if (!_setProperty(qObj, key, value, &accept))
                    return false;
            } else {
                const auto methodO = findSignal(metaObj, propName);
                if (methodO.has_value()) {
                    const auto signature = "2"_ba + methodO->methodSignature();
                    accept = true;
                    if (!PySide::Signal::connect(qObj, signature, value))
                        return false;
                }
            }
            if (!accept) {
                // PYSIDE-1019: Allow any existing attribute in the constructor.
                if (!_setProperty(qObj, key, value, &accept))
                    return false;
            }
        }
        if (allowErrors) {
            PyErr_Clear();
            continue;
        }
        if (!accept) {
            PyErr_Format(PyExc_AttributeError, "'%s' is not a Qt property or a signal",
                         propName.constData());
            return false;
        }
    }
    return true;
}

void registerCleanupFunction(CleanupFunction func)
{
    cleanupFunctionList.push(func);
}

void runCleanupFunctions()
{
    while (!cleanupFunctionList.isEmpty()) {
        CleanupFunction f = cleanupFunctionList.pop();
        f();
    }
}

#ifdef Py_GIL_DISABLED
static bool inOtherRunningThread(const QObject *qobject)
{
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::OwnerThreadDeletion))
        return false;
    QThread *owner = qobject->thread();
    return owner != nullptr && owner != QThread::currentThread() && owner->isRunning();
}

// Deletes an object whose deferred deletion may have come after the last flush
// of its finishing owner, once the owner has finished. It never waits: a slot
// of the owner's finished() may wait for the caller. The main thread asks again.
static void deleteWhenOwnerFinished(const QPointer<QObject> &object,
                                    const QPointer<QThread> &owner)
{
    if (!owner.isNull() && !owner->wait(QDeadlineTimer(0))) {
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        auto again = [object, owner] { deleteWhenOwnerFinished(object, owner); };
        if (app->thread() == QThread::currentThread())
            QTimer::singleShot(std::chrono::milliseconds(10), app, again);
        else
            QMetaObject::invokeMethod(app, again, Qt::QueuedConnection);
        return;
    }
    if (object.isNull())
        return;
    QCoreApplication::removePostedEvents(object.data(), QEvent::DeferredDelete);
    delete object.data();
}

static void deleteLaterEntry(void *qobject)
{
    auto *object = static_cast<QObject *>(qobject);
    Shiboken::failpoint("delete-later-before-post");
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::FinishedOwnerCleanup)) {
        object->deleteLater();
        return;
    }
    // A finishing thread flushes its deferred deletions once; an event posted
    // after that never runs. Still running after the post, it has not flushed.
    QPointer<QThread> owner(object->thread());
    QPointer<QObject> guard(object);
    object->deleteLater();
    if (owner.isNull() || !owner->isRunning())
        deleteWhenOwnerFinished(guard, owner);
}

// Hook of setDeleteInOwnerThread(): a running owner thread deletes its QObject itself.
static void deleteInOwnerThread(SbkObject *sbkObj, Shiboken::DestructorEntry &entry)
{
    PyTypeObject *qObjType = PySide::qObjectType();
    if (!PyType_IsSubtype(Py_TYPE(sbkObj), qObjType)
        || Shiboken::Object::cppPointer(sbkObj, qObjType) != entry.cppInstance) {
        return;
    }
    void *qobject = Shiboken::Conversions::cppPointer(qObjType, sbkObj);
    if (inOtherRunningThread(static_cast<QObject *>(qobject)))
        entry = {deleteLaterEntry, qobject};
}
#endif // Py_GIL_DISABLED

static void destructionVisitor(SbkObject *pyObj, void *data)
{
    auto *realData = reinterpret_cast<void **>(data);
    auto *pyQApp = reinterpret_cast<SbkObject *>(realData[0]);
    auto *pyQObjectType = reinterpret_cast<PyTypeObject *>(realData[1]);

    auto *ob = reinterpret_cast<PyObject *>(pyObj);
    if (pyObj != pyQApp && PyObject_TypeCheck(ob, pyQObjectType)) {
        if (Shiboken::Object::hasOwnership(pyObj) && Shiboken::Object::isValid(pyObj, false)) {
#ifdef Py_GIL_DISABLED
            // Another running thread's object stays; its wrapper hands it over later.
            if (inOtherRunningThread(static_cast<QObject *>(
                    Shiboken::Conversions::cppPointer(pyQObjectType, pyObj)))) {
                return;
            }
#endif
            Shiboken::Object::setValidCpp(pyObj, false);

            Py_BEGIN_ALLOW_THREADS
            Shiboken::callCppDestructor<QObject>(Shiboken::Object::cppPointer(pyObj, pyQObjectType));
            Py_END_ALLOW_THREADS
        }
    }

};

void destroyQCoreApplication()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;

    Shiboken::BindingManager &bm = Shiboken::BindingManager::instance();
    // Free-threaded, held across the walk, which compares against it.
    auto pyQAppRef = bm.wrapperRef(app);
    SbkObject *pyQApp = pyQAppRef.object();
    PyTypeObject *pyQObjectType = PySide::qObjectType();
    assert(pyQObjectType);

    void *data[2] = {pyQApp, pyQObjectType};
    bm.visitAllPyObjects(&destructionVisitor, &data);

    // in the end destroy app
    // Allow threads because the destructor calls
    // QThreadPool::globalInstance().waitForDone() which may deadlock on the GIL
    // if there is a worker working with python objects.
    Py_BEGIN_ALLOW_THREADS
    delete app;
    Py_END_ALLOW_THREADS
    // PYSIDE-571: make sure to create a singleton deleted qApp.
    Py_DECREF(MakeQAppWrapper(nullptr));
}

std::size_t getSizeOfQObject(PyTypeObject *type)
{
    return retrieveTypeUserData(type)->cppObjSize;
}

static void initDynamicMetaObjectHelper(PyTypeObject *type,
                                        TypeUserData *userData)
{
    Shiboken::ObjectType::setTypeUserData(type, userData, Shiboken::callCppDestructor<TypeUserData>);

    // initialize staticQMetaObject property
    const void *metaObjectPtr = userData->mo.update();
    static SbkConverter *converter = Shiboken::Conversions::getConverter("QMetaObject");
    if (!converter)
        return;
    Shiboken::AutoDecRef pyMetaObject(Shiboken::Conversions::pointerToPython(converter, metaObjectPtr));
    PyObject_SetAttr(reinterpret_cast<PyObject *>(type),
                     Shiboken::PyName::qtStaticMetaObject(), pyMetaObject);
}

void initDynamicMetaObject(PyTypeObject *type, const QMetaObject *base, std::size_t cppObjSize)
{
    initDynamicMetaObjectHelper(type, new TypeUserData(base, cppObjSize));
}

TypeUserData *retrieveTypeUserData(PyTypeObject *pyTypeObj)
{
    if (!SbkObjectType_Check(pyTypeObj))
        return nullptr;
    return reinterpret_cast<TypeUserData *>(Shiboken::ObjectType::getTypeUserData(pyTypeObj));
}

TypeUserData *retrieveTypeUserData(PyObject *pyObj)
{
    auto *pyTypeObj = PyType_Check(pyObj)
        ? reinterpret_cast<PyTypeObject *>(pyObj) : Py_TYPE(pyObj);
    return retrieveTypeUserData(pyTypeObj);
}

const QMetaObject *retrieveMetaObject(PyTypeObject *pyTypeObj)
{
    TypeUserData *userData = retrieveTypeUserData(pyTypeObj);
    return userData ? userData->mo.update() : nullptr;
}

const QMetaObject *retrieveMetaObject(PyObject *pyObj)
{
    auto *pyTypeObj = PyType_Check(pyObj)
        ? reinterpret_cast<PyTypeObject *>(pyObj) : Py_TYPE(pyObj);
    return retrieveMetaObject(pyTypeObj);
}

void initQObjectSubType(PyTypeObject *type, PyObject *args, PyObject * /* kwds */)
{
    PyTypeObject *qObjType = PySide::qObjectType();

    PyObject *bases = PyTuple_GetItem(args, 1);

    TypeUserData *userData = nullptr;

    for (PyTypeObject *base : Shiboken::PyTupleTypeRange(bases)) {
        if (PyType_IsSubtype(base, qObjType)) {
            userData = retrieveTypeUserData(base);
            break;
        }
    }
    if (!userData) {
        const char *className = Shiboken::String::toCString(PyTuple_GetItem(args, 0));
        qWarning("libpyside: A subclass of QObject is not inheriting QObject."
                 " A crash might happen when using %s.", className);
        return;
    }
    // PYSIDE-1463: Don't change feature selection durin subtype initialization.
    // This behavior is observed with PySide 6.
    PySide::Feature::Enable(false);
    // create DynamicMetaObject based on python type
    auto *subTypeData = new TypeUserData(type, userData->mo.update(), userData->cppObjSize);
    initDynamicMetaObjectHelper(type, subTypeData);
    PySide::Feature::Enable(true);
}

extern "C" {
static int qAppInputHook()
{
    auto *app = QCoreApplication::instance();
    if (app == nullptr || app->thread() != QThread::currentThread())
        return 0;
#ifndef Q_OS_WIN
    // Check for press on stdin (file descriptor 0)
    pollfd stdinPfd =  qt_make_pollfd(0, POLLIN);
    while (qt_safe_poll(&stdinPfd, 1, QDeadlineTimer{1}) == 0)
        QCoreApplication::processEvents({}, 50000);
#else
    while (_kbhit() == 0)
        QCoreApplication::processEvents({}, 50000);
#endif
    return 0;
}
} // extern "C"

static void unregisterQAppInputHook()
{
    PyOS_InputHook = nullptr;
}

static void registerQAppInputHook()
{
    PyOS_InputHook = qAppInputHook;
    qAddPostRoutine(unregisterQAppInputHook);
}

/*!
    Sets up qApp for the QtCore module and registers destroyQCoreApplication()
    with libshiboken. Free-threaded builds also install
    Shiboken::setDeleteInOwnerThread().
*/
void initQApp()
{
    /*
     * qApp will not be initialized when embedding is active.
     * That means that qApp exists already when PySide is initialized.
     * We could solve that by creating a qApp variable, but in embedded
     * mode, we also have the effect that the first assignment to qApp
     * is persistent! Therefore, we can never be sure to have created
     * qApp late enough to get the right type for the instance.
     *
     * I would appreciate very much if someone could explain or even fix
     * this issue. It exists only when a pre-existing application exists.
     */
    if (qApp == nullptr) {
        registerQAppInputHook();
        Py_DECREF(MakeQAppWrapper(nullptr));
    }

    // PYSIDE-1470: Register a function to destroy an application from shiboken.
    setDestroyQApplication(destroyQCoreApplication);
#ifdef Py_GIL_DISABLED
    Shiboken::setDeleteInOwnerThread(deleteInOwnerThread);
#endif
}

static PyObject *getHiddenTruePropertyDataFromQObject(PyObject *self, PyObject *name)
{
    // PYSIDE-1889: If we have actually a Python property, return f(get|set|del).
    //              Do not store this attribute in the instance dict, because this
    //              would create confusion with overload.
    // Note: before implementing this property handling, the meta function code
    // below created meta functions which was quite wrong.
    auto *subdict = _PepType_Lookup(Py_TYPE(self), PySideMagicName::property_methods());
    if (PyObject *propName = PyDict_GetItem(subdict, name)) {
        // We really have a property name and need to fetch the fget or fset function.
        static PyObject *const _fget = Shiboken::String::createStaticString("fget");
        static PyObject *const _fset = Shiboken::String::createStaticString("fset");
        static PyObject *const _fdel = Shiboken::String::createStaticString("fdel");
        static PyObject *const arr[3] = {_fget, _fset, _fdel};
        auto *prop = _PepType_Lookup(Py_TYPE(self), propName);
        for (auto *trial : arr) {
            auto *res = PyObject_GetAttr(prop, trial);
            if (res) {
                Shiboken::AutoDecRef elemName(PyObject_GetAttr(res, PySideMagicName::name()));
                // Note: This comparison works because of interned strings.
                if (elemName == name)
                    return res;
                Py_DECREF(res);
            }
            PyErr_Clear();
        }
    }

    return nullptr;
}

static PyObject *getHiddenMetaMethodDataFromQObject(int featureFlags,
                                                    QObject *cppSelf,
                                                    PyObject *self,
                                                    PyObject *attrName)
{
    // Provide methods from non-exposed QMetaObjects
    const char *attrNameC = Shiboken::String::toCString(attrName);
    if (std::strncmp("__", attrNameC, 2) == 0)
        return nullptr;

    const bool snakeFlag = featureFlags & 0x01;
    const QMetaObject *metaObject = cppSelf->metaObject();
    QList<QMetaMethod> signalList;
    // Caution: This inserts a meta function or a signal into the instance dict.
    for (int i = 0, imax = metaObject->methodCount(); i < imax; ++i) {
        QMetaMethod method = metaObject->method(i);
        // PYSIDE-1753: Snake case names must be renamed here too, or they will be
        // found unexpectedly when forgetting to rename them.
        const auto &origName = method.name();
        // Currently, we rename only methods but no signals. This might change.
        const bool isSignal = method.methodType() == QMetaMethod::Signal;
        const QByteArray matchName =
            (snakeFlag && !isSignal) ? _sigWithMangledName(origName, true) : origName;
        if (attrNameC == matchName) {
            if (isSignal) {
                signalList.append(method);
            } else if (auto *func = MetaFunction::newObject(cppSelf, i)) {
                auto *result = reinterpret_cast<PyObject *>(func);
                PyObject_SetAttr(self, attrName, result);
                return result;
            }
        }
    }

    if (!signalList.isEmpty()) {
        auto *pySignal = reinterpret_cast<PyObject *>(
            Signal::newObjectFromMethod(cppSelf, self, signalList));
        PyObject_SetAttr(self, attrName, pySignal);
        return pySignal;
    }

    return nullptr;
}

/*!
    Checks for properties and signals registered on the meta object and
    returns these. Also handles Python properties when \c true_property
    was selected.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a cppSelf
            \li The QObject which contains the meta object.
        \row
            \li \a self
            \li Python object of \a cppSelf.
        \row
            \li \a name
            \li Name of the attribute which the function will try to
                retrieve from the meta data.
    \endtable

    Returns the Python object which contains the data obtained from
    the meta object or the Python method pulled out of a Python property.
*/
PyObject *getHiddenDataFromQObject(QObject *cppSelf, PyObject *self, PyObject *name)
{
    // PYSIDE-68-bis: This getattr finds signals early by `signalDescrGet`.
    PyObject *attr = PyObject_GenericGetAttr(self, name);
    if (!Shiboken::Object::isValid(reinterpret_cast<SbkObject *>(self), false))
        return attr;

    if (attr != nullptr) {
        if (Property::checkType(attr)) {
            PyObject *value = Property::getValue(reinterpret_cast<PySideProperty *>(attr), self);
            Py_DECREF(attr);
            return value;
        }
        return attr;
    }

    Shiboken::Errors::Stash errorStash; // Stash the attribute error for later in case the below fails

    const int featureFlags = currentSelectId(Py_TYPE(self));
    if (featureFlags & 0x02) { // True property feature
        attr = getHiddenTruePropertyDataFromQObject(self, name);
        if (attr != nullptr) {
            errorStash.release();
            return attr;
        }
    }

    attr = getHiddenMetaMethodDataFromQObject(featureFlags, cppSelf, self, name);
    if (attr != nullptr)
        errorStash.release();
    return attr;
}

QMutex &nextQObjectMemoryAddrMutex()
{
    static QMutex mutex;
    return mutex;
}

void *nextQObjectMemoryAddr()
{
    return qobjectNextAddr;
}

void setNextQObjectMemoryAddr(void *addr)
{
    qobjectNextAddr = addr;
}

} // namespace PySide

// A std::shared_ptr is used with a deletion function to invalidate a pointer
// when the property value is cleared.  This should be a QSharedPointer with
// a void *pointer, but that isn't allowed.
// We used to have a Q_DECLARE_METATYPE of the QSharedPointer<any_t> but
// it was removed due to the obsolescence of the macro.
using any_t = char;

namespace PySide
{

static void invalidatePtr(any_t *object)
{
    // PYSIDE-2254: Guard against QObjects outliving Python, for example the
    // adopted main thread as returned by QObjects::thread().
    if (Py_IsInitialized() == 0)
        return;

    // Check for existence before locking (fix hang when QObjects
    // are moved to different threads).
    auto &bindingManager = Shiboken::BindingManager::instance();
    if (bindingManager.hasWrapper(object)) {
        Shiboken::GilState state;
        if (auto wrapper = bindingManager.wrapperRef(object))
            bindingManager.releaseWrapper(wrapper.object());
    }
}

static const char invalidatePropertyName[] = "_PySideInvalidatePtr";

// PYSIDE-2749: Skip over internal QML classes and classes
// with dynamic meta objects when looking for the best matching
// type to avoid unnessarily triggering the lazy load mechanism
// for classes that do not have a binding from things like eventFilter().
static inline bool isInternalObject(const char *name)
{
    return std::strstr(name, "QMLTYPE") != nullptr || std::strstr(name, "QQmlPrivate") != nullptr;
}

static const QMetaObject *metaObjectCandidate(const QObject *o)
{
    const auto *metaObject = o->metaObject();
    // Skip QML helper types and Python objects
    if (hasDynamicMetaObject(o)) {
        if (const auto *super = metaObject->superClass())
            metaObject = super;
    }
    for (const auto *candidate = metaObject; candidate != nullptr; candidate = candidate->superClass()) {
        if (!isInternalObject(candidate->className())) {
            metaObject = candidate;
            break;
        }
    }
    return metaObject;
}

// PYSIDE-1214, when creating new wrappers for classes inheriting QObject but
// not exposed to Python, try to find the best-matching (most-derived) Qt
// class by walking up the meta objects.
static const char *typeName(const QObject *cppSelf)
{
    for (const auto *metaObject = metaObjectCandidate(cppSelf); metaObject != nullptr;
         metaObject = metaObject->superClass()) {
        const char *name = metaObject->className();
        if (Shiboken::Conversions::getConverter(name))
            return name;
    }
    // PYSIDE-3409: Fallback to typeid(). Note some classes in QtWebEngine
    // do not have RTTI, so, typeid() can crash. Hence do QMetaObject lookup
    // first.
    return typeid(*cppSelf).name();
}

PyTypeObject *getTypeForQObject(const QObject *cppSelf)
{
    // First check if there are any instances of Python implementations
    // inheriting a PySide class.
#ifdef Py_GIL_DISABLED
    // The type only: holding the wrapper could delete the object mid-walk.
    if (auto *type = Shiboken::BindingManager::instance().wrapperTypeRef(cppSelf)) {
        Py_DECREF(type);  // borrowed, as before; the class outlives the lookup
        return type;
    }
#else
    auto *existing = Shiboken::BindingManager::instance().retrieveWrapper(cppSelf);
    if (existing != nullptr)
        return reinterpret_cast<PyObject *>(existing)->ob_type;
#endif
    // Find the best match (will return a PySide type)
    return Shiboken::ObjectType::typeForTypeName(typeName(cppSelf));
}

PyObject *getWrapperForQObject(QObject *cppSelf, PyTypeObject *sbk_type)
{
    if (auto wrapper = Shiboken::BindingManager::instance().wrapperRef(cppSelf))
        return wrapper.newRef();

    // Setting the property will trigger an QEvent notification, which may call into
    // code that creates the wrapper so only set the property if it isn't already
    // set and check if it's created after the set call
    QVariant existing = cppSelf->property(invalidatePropertyName);
    if (!existing.isValid()) {
        if (cppSelf->thread() == QThread::currentThread()) {
            std::shared_ptr<any_t> shared_with_del(reinterpret_cast<any_t *>(cppSelf), invalidatePtr);
            cppSelf->setProperty(invalidatePropertyName, QVariant::fromValue(shared_with_del));
        }
        if (auto wrapper = Shiboken::BindingManager::instance().wrapperRef(cppSelf))
            return wrapper.newRef();
    }

    return Shiboken::Object::newObjectWithHeuristics(sbk_type, cppSelf, false, typeName(cppSelf));
}

static const unsigned char qt_resource_name[] = {
  // qt
  0x0,0x2,
  0x0,0x0,0x7,0x84,
  0x0,0x71,
  0x0,0x74,
    // etc
  0x0,0x3,
  0x0,0x0,0x6c,0xa3,
  0x0,0x65,
  0x0,0x74,0x0,0x63,
    // qt.conf
  0x0,0x7,
  0x8,0x74,0xa6,0xa6,
  0x0,0x71,
  0x0,0x74,0x0,0x2e,0x0,0x63,0x0,0x6f,0x0,0x6e,0x0,0x66
};

static const unsigned char qt_resource_struct[] = {
  // :
  0x0,0x0,0x0,0x0,0x0,0x2,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x1,
  // :/qt
  0x0,0x0,0x0,0x0,0x0,0x2,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x2,
  // :/qt/etc
  0x0,0x0,0x0,0xa,0x0,0x2,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x3,
  // :/qt/etc/qt.conf
  0x0,0x0,0x0,0x16,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x0
};

bool registerInternalQtConf()
{
    // Guard to ensure single registration.
#ifdef PYSIDE_QT_CONF_PREFIX
    static bool registrationAttempted = false;
#else
    static bool registrationAttempted = true;
#endif
    static bool isRegistered = false;
    if (registrationAttempted)
        return isRegistered;
    registrationAttempted = true;

    // Support PyInstaller case when a qt.conf file might be provided next to the generated
    // PyInstaller executable.
    // This will disable the internal qt.conf which points to the PySide6 subdirectory (due to the
    // subdirectory not existing anymore).
#ifndef PYPY_VERSION
    QString executablePath = QString::fromWCharArray(PyUnicode_AsWideCharString(PySys_GetObject("executable"), nullptr));
#else
    // PYSIDE-535: FIXME: Add this function when available.
    QString executablePath = QLatin1StringView("missing Py_GetProgramFullPath");
#endif // PYPY_VERSION

    QString appDirPath = QFileInfo(executablePath).absolutePath();

    QString maybeQtConfPath = QDir(appDirPath).filePath(u"qt.conf"_s);
    maybeQtConfPath = QDir::toNativeSeparators(maybeQtConfPath);
    bool executableQtConfAvailable = QFileInfo::exists(maybeQtConfPath);

    QString maybeQt6ConfPath = QDir(appDirPath).filePath(u"qt6.conf"_s);
    maybeQt6ConfPath = QDir::toNativeSeparators(maybeQt6ConfPath);
    bool executableQt6ConfAvailable = QFileInfo::exists(maybeQt6ConfPath);

    // Allow disabling the usage of the internal qt.conf. This is necessary for tests to work,
    // because tests are executed before the package is installed, and thus the Prefix specified
    // in qt.conf would point to a not yet existing location.
    bool disableInternalQtConf =
        qEnvironmentVariableIntValue("PYSIDE_DISABLE_INTERNAL_QT_CONF") > 0;
    bool runsInConda =
        qEnvironmentVariableIsSet("CONDA_DEFAULT_ENV") || qEnvironmentVariableIsSet("CONDA_PREFIX");

    if ((!runsInConda && (disableInternalQtConf || executableQtConfAvailable))
        || (runsInConda && executableQt6ConfAvailable)) {
        registrationAttempted = true;
        return false;
    }

    PyObject *pysideModule = PyImport_ImportModule("PySide6");
    if (!pysideModule)
        return false;

    // Querying __file__ should be done only for modules that have finished their initialization.
    // Thus querying for the top-level PySide6 package works for us whenever any Qt-wrapped module
    // is loaded.
    PyObject *pysideInitFilePath = PyObject_GetAttr(pysideModule, Shiboken::PyMagicName::file());
    Py_DECREF(pysideModule);
    if (!pysideInitFilePath)
        return false;

    QString initPath = pyStringToQString(pysideInitFilePath);
    Py_DECREF(pysideInitFilePath);
    if (initPath.isEmpty())
        return false;

    // pysideDir - absolute path to the directory containing the init file, which also contains
    // the rest of the PySide6 modules.
    // prefixPath - absolute path to the directory containing the installed Qt (prefix).
    QDir pysideDir = QFileInfo(QDir::fromNativeSeparators(initPath)).absoluteDir();
    QString setupPrefix;
#ifdef PYSIDE_QT_CONF_PREFIX
    setupPrefix = QStringLiteral(PYSIDE_QT_CONF_PREFIX);
#endif
    const QByteArray prefixPath = pysideDir.absoluteFilePath(setupPrefix).toUtf8();

    // rccData needs to be static, otherwise when it goes out of scope, the Qt resource system
    // will point to invalid memory.
    static QByteArray rccData = QByteArrayLiteral("[Paths]\nPrefix = ") + prefixPath + "\n";
#ifdef Q_OS_WIN
    // LibraryExecutables needs to point to Prefix instead of ./bin because we don't
    // currently conform to the Qt default directory layout on Windows. This is necessary
    // for QtWebEngineCore to find the location of QtWebEngineProcess.exe.
    rccData += QByteArrayLiteral("LibraryExecutables = ") + prefixPath + "\n";
#endif

    // The RCC data structure expects a 4-byte size value representing the actual data.
    qsizetype size = rccData.size();

    for (int i = 0; i < 4; ++i) {
        rccData.prepend((size & 0xff));
        size >>= 8;
    }

    const int version = 0x01;
    isRegistered = qRegisterResourceData(version, qt_resource_struct, qt_resource_name,
                                         reinterpret_cast<const unsigned char *>(
                                             rccData.constData()));

    return isRegistered;
}

PyTypeObject *qObjectType()
{
    PyTypeObject *result = globals()->qobjectType;
    Q_ASSERT(result);
    return result;
}

void setQObjectType(PyTypeObject *t)
{
    globals()->qobjectType = t;
}

bool isQObjectDerived(PyTypeObject *pyType, bool raiseError)
{
    const bool result = PyType_IsSubtype(pyType, qObjectType());
    if (!result && raiseError) {
        PyErr_Format(PyExc_TypeError, "A type inherited from %s expected, got %s.",
                     qObjectType()->tp_name, pyType->tp_name);
    }
    return result;
}

QObject *convertToQObject(PyObject *object, bool raiseError)
{
    if (object == nullptr) {
        if (raiseError)
            PyErr_Format(PyExc_TypeError, "None passed for QObject");
        return nullptr;
    }

    if (!isQObjectDerived(Py_TYPE(object), raiseError))
        return nullptr;

    auto *sbkObject = reinterpret_cast<SbkObject*>(object);
    auto *ptr = Shiboken::Object::cppPointer(sbkObject, qObjectType());
    if (ptr == nullptr) {
        if (raiseError) {
            PyErr_Format(PyExc_TypeError, "Conversion of %s to QObject failed.",
                         Py_TYPE(object)->tp_name);
        }
        return nullptr;
    }
    return reinterpret_cast<QObject*>(ptr);
}

using MetaTypeIf2QMetaObjectHash = QHash<const QtPrivate::QMetaTypeInterface *, const QMetaObject *>;

Q_GLOBAL_STATIC(MetaTypeIf2QMetaObjectHash, metaTypeIf2QMetaObjectHash);

static const QMetaObject *metaObjectFunc(const QtPrivate::QMetaTypeInterface *mif)
{
    return metaTypeIf2QMetaObjectHash()->value(mif);
}

QMetaType createQObjectPtrMetaType(const QMetaObject *metaObject)
{
    const char *className = metaObject->className();
    const auto nameLen = std::strlen(className);
    char *ptrName = new char[nameLen + 2];
    std::strcpy(ptrName, className);
    ptrName[nameLen] = '*';
    ptrName[nameLen + 1] = '\0';

    if (auto existing = QMetaType::fromName(ptrName); existing.isValid()) {
        delete [] ptrName;
        return existing;
    }

    auto *mti = new QtPrivate::QMetaTypeInterface {
        1,       // revision
        ushort(std::alignment_of<QObject*>()),
        sizeof(QObject*),
        uint(QMetaType::IsPointer | QMetaType::RelocatableType | QMetaType::PointerToQObject),
        {},      // typeId
        metaObjectFunc,
        ptrName,
        nullptr, // ctr
        nullptr, // copyCtr
        nullptr, // moveCtr
        nullptr, // dtor
        QtPrivate::QEqualityOperatorForType<QObject*>::equals,
        QtPrivate::QLessThanOperatorForType<QObject*>::lessThan,
        nullptr, // qDebug
        nullptr, // dataStreamOut
        nullptr, // dataStreamIn
        nullptr  // legacyRegisterOp
    };

    metaTypeIf2QMetaObjectHash()->insert(mti, metaObject);

    QMetaType metaType(mti);
    [[maybe_unused]] const int id = metaType.id(); // enforce registration
    return metaType;
}

// -----------------------------------------------------------------------------


QMetaType qMetaTypeFromPyType(PyTypeObject *pyType)
{
    if (Shiboken::String::checkType(pyType))
        return QMetaType(QMetaType::QString);
    if (pyType == &PyFloat_Type)
        return QMetaType(QMetaType::Double);
    if (pyType == &PyLong_Type)
        return QMetaType(QMetaType::Int);
    if (Shiboken::ObjectType::checkType(pyType))
        return QMetaType::fromName(Shiboken::ObjectType::getOriginalName(pyType));
    return QMetaType::fromName(PepType_GetFullyQualifiedNameStr(pyType));
}

QDebug operator<<(QDebug debug, const PySide::debugQObject &qo)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();

    if (qo.m_qobject == nullptr) {
        debug << "QObject(0)";
    } else {
        debug << qo.m_qobject->metaObject()->className() << '/';
        if (const auto &on = qo.m_qobject->objectName(); !on.isEmpty())
            debug << '"' << on << '"';
        else
            debug << static_cast<const void *>(qo.m_qobject);
    }
    return debug;
}

} // namespace PySide

void deferredDeleteQObject(void *cppSelf)
{
    if (cppSelf == nullptr)
        return;
    auto *qobject = reinterpret_cast<QObject *>(cppSelf);
    auto *ownerThread = qobject->thread();
    const auto *currentThread = QThread::currentThread();
    // Directly delete when called from ownerThread
    if (ownerThread == nullptr || ownerThread == currentThread
        || QCoreApplication::closingDown()) {
        delete qobject;
        return;
    }

    qCWarning(lcPySide).noquote().nospace()
        << "libpyside: Deferred deletion of " << PySide::debugQObject(qobject)
        << "(owner thread: " << PySide::debugQObject(ownerThread)
        << "), GC thread: " << PySide::debugQObject(currentThread);

    // Owner thread has event loop: Schedule deleteLater()
    if (QAbstractEventDispatcher::instance(ownerThread) != nullptr) {
        qobject->deleteLater();
        return;
    }

    // No event loop: Fall back to deletion in main thread
    if (QThread::isMainThread()) {
        delete qobject;
        return;
    }

    auto &bm = Shiboken::BindingManager::instance();
    bm.addToDeletionInMainThread({Shiboken::callCppDestructor<QObject>, cppSelf});
    bm.runDeletionInMainThread();
}
