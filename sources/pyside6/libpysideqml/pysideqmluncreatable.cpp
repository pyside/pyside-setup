// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysideqmluncreatable.h"
#include <pysideclassdecorator_p.h>
#include <pysideclassinfo.h>

#include <sbkcppstring.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtCore/qbytearray.h>
#include <private/qmetaobjectbuilder_p.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Qt::StringLiterals;

class PySideQmlUncreatablePrivate : public PySide::ClassDecorator::StringDecoratorPrivate
{
public:
    PyObject *tp_call(PyObject *self, PyObject *args, PyObject * /* kw */) override;
    int tp_init(PyObject *self, PyObject *args, PyObject *kwds) override;
    const char *name() const override;
};

const char *PySideQmlUncreatablePrivate::name() const
{
    return "QmlUncreatable";
}

// The call operator is passed the class type and registers the reason
// in the uncreatableReasonMap()
PyObject *PySideQmlUncreatablePrivate::tp_call(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    PyObject *klass = tp_call_check(args, CheckMode::WrappedType);
    if (klass== nullptr)
        return nullptr;

    auto *type = reinterpret_cast<PyTypeObject *>(klass);
    auto *data = DecoratorPrivate::get<PySideQmlUncreatablePrivate>(self);
    setUncreatableClassInfo(type, data->string());

    Py_INCREF(klass);
    return klass;
}

int PySideQmlUncreatablePrivate::tp_init(PyObject *self, PyObject *args, PyObject * /* kwds */)
{
    int result = -1;
    const auto argsCount = PyTuple_Size(args);
    if (argsCount == 0) {
        result = 0; // QML-generated reason
    } else if (argsCount == 1) {
        PyObject *arg = PyTuple_GetItem(args, 0);
        result = arg == Py_None
            ? 0 // QML-generated reason
            : convertToString(self, args);
    }

    if (result != 0) {
        PyErr_Format(PyExc_TypeError,
                     "QmlUncreatable() takes a single string argument or no argument");
    }

    return result;
}

extern "C" {

PyTypeObject *createPySideQmlUncreatableType(void)
{
    auto typeSlots =
        PySide::ClassDecorator::Methods<PySideQmlUncreatablePrivate>::typeSlots();

    PyType_Spec PySideQmlUncreatableType_spec = {
        "2:PySide6.QtCore.QmlUncreatable",
        sizeof(PySideClassDecorator),
        0,
        Py_TPFLAGS_DEFAULT,
        typeSlots.data()
    };
    return SbkType_FromSpec(&PySideQmlUncreatableType_spec);
}

PyTypeObject *PySideQmlUncreatable_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createPySideQmlUncreatableType);
#else
    static auto *type = createPySideQmlUncreatableType();
#endif
    return type;
}

} // extern "C"

static const char *qmlUncreatable_SignatureStrings[] = {
    "PySide6.QtQml.QmlUncreatable(self,reason:str)",
    nullptr // Sentinel
};

void initQmlUncreatable(PyObject *module)
{
    auto *qmlUncreatableType = PySideQmlUncreatable_TypeF();
    if (InitSignatureStrings(qmlUncreatableType, qmlUncreatable_SignatureStrings) < 0)
        return;

    auto *obQmlUncreatableType = reinterpret_cast<PyObject *>(qmlUncreatableType);
    Py_INCREF(obQmlUncreatableType);
    PepModule_AddType(module, qmlUncreatableType);
}

/*!
    Sets the class info marking the Python \a type as uncreatable
    with the given \a reason.
*/
void setUncreatableClassInfo(PyTypeObject *type, const QByteArray &reason)
{
    PySide::ClassInfo::setClassInfo(type, {
        {"QML.Creatable"_ba, "false"_ba},
        {"QML.UncreatableReason"_ba, reason} });
}

/*!
    \overload

    Adds the class info marking a type as uncreatable with the given
    \a reason to the meta object \a builder.
*/
void setUncreatableClassInfo(QMetaObjectBuilder *builder, const QByteArray &reason)
{
    builder->addClassInfo("QML.Creatable", "false");
    builder->addClassInfo("QML.UncreatableReason", reason);
}
