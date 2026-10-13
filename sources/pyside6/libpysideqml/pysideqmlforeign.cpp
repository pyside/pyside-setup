// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmlforeign_p.h"
#include "pysideqmltypeinfo_p.h"

#include <signalmanager.h>
#include <pysideclassdecorator_p.h>

#include <gilstate.h>
#include <sbkconverter.h>
#include <sbkstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtCore/qdebug.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

// The QmlForeign decorator modifies QmlElement to create a different type
// QmlElement.
class PySideQmlForeignPrivate : public PySide::ClassDecorator::TypeDecoratorPrivate
{
public:
    PyObject *tp_call(PyObject *self, PyObject *args, PyObject * /* kw */) override;
    const char *name() const override;
};

// The call operator is passed the class type and registers the type
// in QmlTypeInfo.
PyObject *PySideQmlForeignPrivate::tp_call(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *klass = tp_call_check(args, CheckMode::WrappedType);
    if (klass == nullptr)
        return nullptr;

    auto *data = DecoratorPrivate::get<PySideQmlForeignPrivate>(self);
    const auto info = PySide::Qml::ensureQmlTypeInfo(klass);
    info->foreignType = data->type();
    // Insert an alias to be used by the factory functions of Decorators like
    // @QmlExtended and @QmlAttached.
    const auto *foreignObj = reinterpret_cast<const PyObject *>(info->foreignType);
    PySide::Qml::insertQmlTypeInfoAlias(foreignObj, info);

    Py_INCREF(klass);
    return klass;
}

const char *PySideQmlForeignPrivate::name() const
{
    return "QmlForeign";
}

extern "C" {

static PyTypeObject *createPySideQmlForeignType()
{
    auto typeSlots =
        PySide::ClassDecorator::Methods<PySideQmlForeignPrivate>::typeSlots();

    PyType_Spec PySideQmlForeignType_spec = {
        "2:PySide6.QtCore.QmlForeign",
        sizeof(PySideClassDecorator),
        0,
        Py_TPFLAGS_DEFAULT,
        typeSlots.data()
    };
    return SbkType_FromSpec(&PySideQmlForeignType_spec);
}

PyTypeObject *PySideQmlForeign_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createPySideQmlForeignType);
#else
    static auto *type = createPySideQmlForeignType();
#endif
    return type;
}

} // extern "C"

static const char *qmlForeign_SignatureStrings[] = {
    "PySide6.QtQml.QmlForeign(self,type:type)",
    nullptr // Sentinel
};

namespace PySide::Qml {

void initQmlForeign(PyObject *module)
{
    auto *foreignType = PySideQmlForeign_TypeF();
    if (InitSignatureStrings(foreignType, qmlForeign_SignatureStrings) < 0)
        return;

    auto *obForeignType = reinterpret_cast<PyObject *>(foreignType);
    Py_INCREF(obForeignType);
    PepModule_AddType(module, foreignType);
}

} // namespace PySide::Qml
