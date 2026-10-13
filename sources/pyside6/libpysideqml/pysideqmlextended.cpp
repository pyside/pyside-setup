// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmlextended_p.h"
#include "pysideqmltypeinfo_p.h"
#include "pysideqmlregistertype_p.h"

#include <pyside_p.h>
#include <pysideclassdecorator_p.h>
#include <pysideqobject.h>

#include <autodecref.h>
#include <gilstate.h>
#include <sbkconverter.h>
#include <sbkpep.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtQml/qqml.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

// The QmlExtended decorator modifies QmlElement to register an extension.
// Due to the (reverse) execution order of decorators, it needs to follow
// QmlElement.
class PySideQmlExtendedPrivate : public PySide::ClassDecorator::TypeDecoratorPrivate
{
public:
    PyObject *tp_call(PyObject *self, PyObject *args, PyObject * /* kw */) override;
    const char *name() const override;
};

// The call operator is passed the class type and registers the type
// in QmlTypeInfo.
PyObject *PySideQmlExtendedPrivate::tp_call(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *klass = tp_call_check(args, CheckMode::WrappedType);
    if (klass == nullptr)
        return nullptr;

    auto *data = DecoratorPrivate::get<PySideQmlExtendedPrivate>(self);
    PySide::Qml::ensureQmlTypeInfo(klass)->extensionType = data->type();

    Py_INCREF(klass);
    return klass;
}

const char *PySideQmlExtendedPrivate::name() const
{
    return "QmlExtended";
}

extern "C" {

static PyTypeObject *createPySideQmlExtendedType()
{
    auto typeSlots =
        PySide::ClassDecorator::Methods<PySideQmlExtendedPrivate>::typeSlots();

    PyType_Spec PySideQmlExtendedType_spec = {
        "2:PySide6.QtCore.QmlExtended",
        sizeof(PySideClassDecorator),
        0,
        Py_TPFLAGS_DEFAULT,
        typeSlots.data()
    };
    return SbkType_FromSpec(&PySideQmlExtendedType_spec);
}

PyTypeObject *PySideQmlExtended_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createPySideQmlExtendedType);
#else
    static auto *type = createPySideQmlExtendedType();
#endif
    return type;
}

} // extern "C"

static const char *qmlExtended_SignatureStrings[] = {
    "PySide6.QtQml.QmlExtended(self,type:type)",
    nullptr // Sentinel
};

namespace PySide::Qml {

static QObject *extensionFactory(QObject *o)
{
    Shiboken::GilState gilState;
    Shiboken::Conversions::SpecificConverter converter("QObject");
    Q_ASSERT(converter);
    PyObject *pyObj = converter.toPython(static_cast<void*>(&o));
    Q_ASSERT(pyObj);

    // Search for the extension type and create an instance by invoking
    // the call operator on type with the parent parameter.
    // If there is an error and nullptr is returned, a crash occurs,
    // so, errors should at least be printed.

    auto *pyObjType = Py_TYPE(pyObj);
    const auto info = qmlTypeInfo(reinterpret_cast<PyObject *>(pyObjType));
    if (!info || info->extensionType == nullptr) {
        qWarning("QmlExtended: Cannot find extension of %s.",
                 PepType_GetFullyQualifiedNameStr(pyObjType));
        return nullptr;
    }

    Shiboken::AutoDecRef args(PyTuple_New(1));
    PyTuple_SetItem(args.object(), 0, pyObj);
    auto *extensionTypeObj = reinterpret_cast<PyObject *>(info->extensionType);
    Shiboken::AutoDecRef pyResult(PyObject_Call(extensionTypeObj, args, nullptr));
    if (pyResult.isNull() || PyErr_Occurred()) {
        PyErr_Print();
        return nullptr;
    }

    if (PyType_IsSubtype(pyResult->ob_type, PySide::qObjectType()) == 0) {
        qWarning("QmlExtended: Extension objects must inherit QObject, got %s.",
                 PepType_GetFullyQualifiedNameStr(pyResult->ob_type));
        return nullptr;
    }

    QObject *result = nullptr;
    converter.toCpp(pyResult.object(), static_cast<void*>(&result));
    return result;
}

void initQmlExtended(PyObject *module)
{
    auto *qmlExtendedType = PySideQmlExtended_TypeF();
    if (InitSignatureStrings(qmlExtendedType, qmlExtended_SignatureStrings) < 0)
        return;

    auto *obQmlExtendedType = reinterpret_cast<PyObject *>(qmlExtendedType);
    Py_INCREF(obQmlExtendedType);
    PepModule_AddType(module, qmlExtendedType);
}

PySide::Qml::QmlExtensionInfo qmlExtendedInfo(PyObject *t,
                                              const std::shared_ptr<QmlTypeInfo> &info)
{
    PySide::Qml::QmlExtensionInfo result{nullptr, nullptr};
    if (info && info->extensionType) {
        result.metaObject = PySide::retrieveMetaObject(info->extensionType);
        if (result.metaObject) {
            result.factory = extensionFactory;
        } else {
            qWarning("Unable to retrieve meta object for %s",
                     PepType_GetFullyQualifiedNameStr(reinterpret_cast<PyTypeObject *>(t)));
        }
    }
    return result;
}

} // namespace PySide::Qml
