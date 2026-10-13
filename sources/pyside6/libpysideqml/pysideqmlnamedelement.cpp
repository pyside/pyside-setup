// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmlnamedelement_p.h"
#include <pysideclassdecorator_p.h>
#include <pysideqmlregistertype_p.h>

#include <sbktypefactory.h>
#include <signature.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

class PySideQmlNamedElementPrivate : public PySide::ClassDecorator::StringDecoratorPrivate
{
public:
    PyObject *tp_call(PyObject *self, PyObject *args, PyObject * /* kw */) override;
    const char *name() const override;
};

const char *PySideQmlNamedElementPrivate::name() const
{
    return "QmlNamedElement";
}

// The call operator is passed the class type and registers the type
PyObject *PySideQmlNamedElementPrivate::tp_call(PyObject *self, PyObject *args, PyObject *)
{
    PyObject *klass = tp_call_check(args, CheckMode::WrappedType);
    if (klass == nullptr)
        return nullptr;

    auto *data = DecoratorPrivate::get<PySideQmlNamedElementPrivate>(self);
    auto *result = PySide::Qml::qmlNamedElementMacro(klass, data->string());
    Py_XINCREF(result);
    return result;
}

extern "C" {

PyTypeObject *createPySideQmlNamedElementType(void)
{
    auto typeSlots =
        PySide::ClassDecorator::Methods<PySideQmlNamedElementPrivate>::typeSlots();

    PyType_Spec PySideQmlNamedElementType_spec = {
        "2:PySide6.QtCore.QmlNamedElement",
        sizeof(PySideClassDecorator),
        0,
        Py_TPFLAGS_DEFAULT,
        typeSlots.data()
    };
    return SbkType_FromSpec(&PySideQmlNamedElementType_spec);
}

PyTypeObject *PySideQmlNamedElement_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createPySideQmlNamedElementType);
#else
    static auto *type = createPySideQmlNamedElementType();
#endif
    return type;
}

} // extern "C"

static const char *qmlNamedElement_SignatureStrings[] = {
    "PySide6.QtQml.QmlNamedElement(self,reason:str)",
    nullptr // Sentinel
};

void initQmlNamedElement(PyObject *module)
{
    auto *qmlNamedElementType = PySideQmlNamedElement_TypeF();
    if (InitSignatureStrings(qmlNamedElementType, qmlNamedElement_SignatureStrings) < 0)
        return;

    auto *obQmlNamedElementType = reinterpret_cast<PyObject *>(qmlNamedElementType);
    Py_INCREF(obQmlNamedElementType);
    PepModule_AddType(module, qmlNamedElementType);
}
