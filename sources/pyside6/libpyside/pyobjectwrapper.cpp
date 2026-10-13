// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:handling-untrusted-data

#include "pyobjectwrapper.h"
#include "pysideglobals_p.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <gilstate.h>
#include <sbkenum.h>
#include <sbkpep.h>
#include <sbkstring.h>
#include <sbkstaticstrings.h>
#ifdef Py_GIL_DISABLED
#  include <sbkfailpoint.h>
#  include <sbkftoptions.h>
#endif

#include <QtCore/qdebug.h>

using namespace Qt::StringLiterals;

static int pyObjectWrapperMetaTypeId = QMetaType::UnknownType;

// Builds a callable _safe_loads(bytes) -> object restricted to Python builtin types.
static PyObject *createSafeLoadsFunc()
{
    const char *code = R"(import pickle as _P, builtins as _B, io as _I
_S = frozenset({'dict','list','tuple','set','frozenset','str','int','float','bool','bytes','bytearray','complex'})
class _U(_P.Unpickler):
    def find_class(self, m, n):
        if m == 'builtins' and n in _S: return getattr(_B, n)
        raise _P.UnpicklingError(
            f"Deserializing '{m}.{n}' is not allowed. "
            'Only Python builtin types (dict, list, str, int, ...) '
            'can be deserialized from QDataStream.')
def _safe_loads(d): return _U(_I.BytesIO(d)).load()
)";
    Shiboken::AutoDecRef ns(PyDict_New());
    Shiboken::AutoDecRef builtins(PepEval_GetFrameBuiltins());
    PyDict_SetItemString(ns, "__builtins__", builtins.object());
    Shiboken::AutoDecRef result(PyRun_String(code, Py_file_input, ns, ns));
    if (result.isNull()) {
        PyErr_Print();
        return nullptr;
    }
    PyObject *func = PyDict_GetItemString(ns, "_safe_loads");
    Py_XINCREF(func);
    return func;
}

#ifdef Py_GIL_DISABLED
static PyObject *createReduceFunc()
{
    Shiboken::AutoDecRef pickleModule(PyImport_ImportModule("pickle"));
    return PyObject_GetAttr(pickleModule, Shiboken::PyName::dumps());
}

// Two threads streaming first build one helper each; the first published
// wins. It is never released, so callers need no reference.
static PyObject *streamHelper(std::atomic<PyObject *> &slot, PyObject *(*create)())
{
    PyObject *helper = slot.load(std::memory_order_acquire);
    if (helper != nullptr)
        return helper;
    PyObject *candidate = create();
    Shiboken::failpoint("stream-helper-before-publish");
    if (!Shiboken::FreeThreading::optionEnabled(
            Shiboken::FreeThreading::Option::StreamHelperOnce)) {
        slot.store(candidate, std::memory_order_release);
        return candidate;
    }
    if (candidate == nullptr || slot.compare_exchange_strong(helper, candidate,
                                                             std::memory_order_acq_rel)) {
        return candidate;
    }
    Py_DECREF(candidate);
    return helper;
}
#endif // Py_GIL_DISABLED

namespace PySide {

// Set when safe_loads rejects a non-builtin type
// Cleared via checkAndClearPickleRejected() in the qsettings-value glue (GIL held), or
// as a fallback in CopyCppToPythonPyObject for other callers.
// Exceptions set inside Shiboken::GilState (within Py_BEGIN_ALLOW_THREADS) are lost
// when GilState releases because PyEval_SaveThread() detaches the thread state, causing
// PyGILState_Ensure() to create a temporary one. This flag is the only reliable way to
// communicate a deserialization failure back to the GIL-holding caller.
static thread_local bool s_pickleRejected = false;

bool PyObjectWrapper::checkAndClearPickleRejected()
{
    return std::exchange(s_pickleRejected, false);
}

/*!
    \class PySide::PyObjectWrapper
    \brief Thin wrapper for passing PyObject in QVariant.

    It increases the reference count at the constructor but \e not
    at the destructor.
    \inmodule libpyside
*/
PyObjectWrapper::PyObjectWrapper()
    :m_me(Py_None)
{
    // PYSIDE-813: When PYSIDE-164 was solved by adding some thread allowance,
    // this code was no longer protected. It was hard to find this connection.
    // See PYSIDE-813 for details.
    Shiboken::GilState gil;
    Py_XINCREF(m_me);
}

PyObjectWrapper::PyObjectWrapper(PyObject *me)
    : m_me(me)
{
    Shiboken::GilState gil;
    Py_XINCREF(m_me);
}

PyObjectWrapper::PyObjectWrapper(const PyObjectWrapper &other)
    : m_me(other.m_me)
{
    Shiboken::GilState gil;
    Py_XINCREF(m_me);
}

PyObjectWrapper::PyObjectWrapper(PyObjectWrapper &&other) noexcept
 : m_me{std::exchange(other.m_me, nullptr)}
{
}

PyObjectWrapper &PyObjectWrapper::operator=(PyObjectWrapper &&other) noexcept
{
    m_me = std::exchange(other.m_me, nullptr);
    return *this;
}

PyObjectWrapper::~PyObjectWrapper()
{
    // Check that Python is still initialized as sometimes this is called by a static destructor
    // after Python interpeter is shutdown.
    if (!Py_IsInitialized())
        return;

    Shiboken::GilState gil;
    Py_XDECREF(m_me);
}

void PyObjectWrapper::reset(PyObject *o)
{
    Shiboken::GilState gil;
    Py_XINCREF(o);
    Py_XDECREF(m_me);
    m_me = o;
}

PyObjectWrapper &PyObjectWrapper::operator=(const PySide::PyObjectWrapper &other)
{
    if (this != &other)
        reset(other.m_me);
    return *this;
}

PyObjectWrapper::operator PyObject *() const
{
    return m_me;
}

int PyObjectWrapper::metaTypeId()
{
    return pyObjectWrapperMetaTypeId;
}

void PyObjectWrapper::registerMetaType()
{
    // Register PyObject type to use in queued signal and slot connections
    pyObjectWrapperMetaTypeId = qRegisterMetaType<PyObjectWrapper>("PyObject");
}

int PyObjectWrapper::toInt() const
{
    // hold the GIL
    Shiboken::GilState state;
    return Shiboken::Enum::check(m_me) ? Shiboken::Enum::getValue(m_me) : -1;
}

QDataStream &operator<<(QDataStream &out, const PyObjectWrapper &myObj)
{
    if (Py_IsInitialized() == 0) {
        qWarning("libpyside: Stream operator for PyObject called without python interpreter.");
        return out;
    }

#ifdef Py_GIL_DISABLED
    Shiboken::GilState gil;
    PyObject *reduce_func = streamHelper(PySide::globals()->pickleReduceFunc, createReduceFunc);
#else
    PyObject *&reduce_func = PySide::globals()->pickleReduceFunc;

    Shiboken::GilState gil;
    if (!reduce_func) {
        Shiboken::AutoDecRef pickleModule(PyImport_ImportModule("pickle"));
        reduce_func = PyObject_GetAttr(pickleModule, Shiboken::PyName::dumps());
    }
#endif
    PyObject *pyObj = myObj;
    Shiboken::AutoDecRef repr(PyObject_CallFunctionObjArgs(reduce_func, pyObj, nullptr));
    if (repr.object()) {
        const char *buff = nullptr;
        Py_ssize_t size  = 0;
        if (PyBytes_Check(repr.object())) {
            buff = PyBytes_AsString(repr.object());
            size = PyBytes_Size(repr.object());
        } else if (Shiboken::String::check(repr.object())) {
            buff = Shiboken::String::toCString(repr.object());
            size = Shiboken::String::len(repr.object());
        }
        QByteArray data(buff, size);
        out << data;
    }
    return out;
}

QDataStream &operator>>(QDataStream &in, PyObjectWrapper &myObj)
{
    if (Py_IsInitialized() == 0) {
        qWarning("libpyside: Stream operator for PyObject called without python interpreter.");
        return in;
    }

#ifdef Py_GIL_DISABLED
    Shiboken::GilState gil;
    PyObject *safe_loads = streamHelper(PySide::globals()->pickleSafeLoadsFunc,
                                        createSafeLoadsFunc);
#else
    PyObject *&safe_loads = PySide::globals()->pickleSafeLoadsFunc;

    Shiboken::GilState gil;
    if (!safe_loads)
        safe_loads = createSafeLoadsFunc();
#endif

    QByteArray repr;
    in >> repr;
    Shiboken::AutoDecRef pyCode(PyBytes_FromStringAndSize(repr.data(), repr.size()));
    Shiboken::AutoDecRef value(PyObject_CallFunctionObjArgs(safe_loads, pyCode.object(), nullptr));
    if (!value.object()) {
        // _safe_loads raised UnpicklingError (non-builtin type) or another error.
        // The exception is on the temporary thread state created by GilState inside
        // Py_BEGIN_ALLOW_THREADS and will be lost when GilState releases. Set the
        // C++-side flag and stream status so callers can raise from the real thread state.
        PyErr_Clear();
        in.setStatus(QDataStream::ReadCorruptData);
        s_pickleRejected = true;
        value.reset(Py_None);
        Py_INCREF(Py_None);
    }
    myObj.reset(value);
    return in;
}

PYSIDE_API QDebug operator<<(QDebug debug, const PyObjectWrapper &myObj)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    // Do not repeat the type name as it is typically called from the QVariant debug
    // operator, which outputs the type.
    debug << '<';
    if (PyObject *ob = myObj) {
        const auto refs = Py_REFCNT(ob);
        debug << PepType_GetFullyQualifiedNameStr(Py_TYPE(ob)) << " at " << ob;
        if (refs == UINT_MAX) // _Py_IMMORTAL_REFCNT
            debug << ", immortal";
        else
            debug << ", refs=" << refs;
    } else {
        debug << '0';
    }
    debug << '>';
    return debug;
}

} // namespace PySide
