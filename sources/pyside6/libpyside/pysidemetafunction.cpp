// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidemetafunction.h"
#include "pysidemetafunction_p.h"

#include "pyobjectwrapper.h"

#include <autodecref.h>
#include <basewrapper.h>
#include <sbkconverter.h>
#include <sbkpep.h>
#include <sbktypefactory.h>
#include <signature.h>

#include <QtCore/qmetaobject.h>
#include <QtCore/qobject.h>
#include <QtCore/qvarlengtharray.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

using namespace Qt::StringLiterals;

extern "C"
{

struct PySideMetaFunctionPrivate
{
    QObject *qobject;
    int methodIndex;
};

//methods
static void functionFree(void *);
static PyObject *functionCall(PyObject *, PyObject *, PyObject *);

static PyTypeObject *createMetaFunctionType()
{
    PyType_Slot PySideMetaFunctionType_slots[] = {
        {Py_tp_call, reinterpret_cast<void *>(functionCall)},
        {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
        {Py_tp_free, reinterpret_cast<void *>(functionFree)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Sbk_object_dealloc)},
        {0, nullptr}
    };

    PyType_Spec PySideMetaFunctionType_spec = {
        "2:PySide6.QtCore.MetaFunction",
        sizeof(PySideMetaFunction),
        0,
        Py_TPFLAGS_DEFAULT,
        PySideMetaFunctionType_slots,
    };

    return SbkType_FromSpec(&PySideMetaFunctionType_spec);
}

PyTypeObject *PySideMetaFunction_TypeF(void)
{
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<PyTypeObject *> typeOnce;
    PyTypeObject *type = typeOnce.get(createMetaFunctionType);
#else
    static auto *type = createMetaFunctionType();
#endif
    return type;
}

void functionFree(void *self)
{
    auto *function = reinterpret_cast<PySideMetaFunction *>(self);
    delete function->d;
}

PyObject *functionCall(PyObject *self, PyObject *args, PyObject * /* kw */)
{
    auto *function = reinterpret_cast<PySideMetaFunction *>(self);

    PyObject *retVal{};
    if (!PySide::MetaFunction::call(function->d->qobject, function->d->methodIndex, args, &retVal))
        return nullptr;
    return retVal;
}

} // extern "C"

/*!
    \namespace PySide::MetaFunction
    \brief Namespace for meta functions.
    \inmodule libpyside
*/
namespace PySide::MetaFunction {

static const char *MetaFunction_SignatureStrings[] = {
    "PySide6.QtCore.MetaFunction.__call__(self,*args:typing.Any)->typing.Any",
    nullptr}; // Sentinel

void init(PyObject *module)
{
    if (InitSignatureStrings(PySideMetaFunction_TypeF(), MetaFunction_SignatureStrings) < 0)
        return;

    auto *metaFunctionType = PySideMetaFunction_TypeF();
    auto *obMetaFunctionType = reinterpret_cast<PyObject *>(metaFunctionType);
    Py_INCREF(obMetaFunctionType);
    PepModule_AddType(module, metaFunctionType);
}

PySideMetaFunction *newObject(QObject *source, int methodIndex)
{
    if (methodIndex >= source->metaObject()->methodCount())
        return nullptr;

    QMetaMethod method = source->metaObject()->method(methodIndex);
    if ((method.methodType() == QMetaMethod::Slot) ||
        (method.methodType() == QMetaMethod::Method)) {
        PySideMetaFunction *function = PyObject_New(PySideMetaFunction, PySideMetaFunction_TypeF());
        function->d = new PySideMetaFunctionPrivate();
        function->d->qobject = source;
        function->d->methodIndex = methodIndex;
        return function;
    }
    return nullptr;
}

bool call(QObject *self, int methodIndex, PyObject *args, PyObject **retVal)
{

    QMetaMethod method = self->metaObject()->method(methodIndex);
    const int parameterCount = method.parameterCount();

    // args given plus return type
    Shiboken::AutoDecRef sequence(PySequence_Fast(args, nullptr));
    const qsizetype numArgs = PySequence_Size(sequence.object()) + 1;

    if (numArgs - 1 > parameterCount) {
        PyErr_Format(PyExc_TypeError, "%s only accepts %d argument(s), %d given!",
                     method.methodSignature().constData(), parameterCount, int(numArgs - 1));
        return false;
    }

    if (numArgs - 1 < parameterCount) {
        PyErr_Format(PyExc_TypeError, "%s needs %d argument(s), %d given!",
                     method.methodSignature().constData(), parameterCount, int(numArgs - 1));
        return false;
    }

    QVarLengthArray<QVariant> methValues(numArgs);
    QVarLengthArray<void *> methArgs(numArgs, nullptr);

    // Leave room for return type
    for (qsizetype i = 0; i < numArgs; ++i) {
        const int argIndex = int(i - 1);
        const int metaTypeId = i == 0 ?  method.returnType() : method.parameterType(argIndex);
        if (i == 0 && metaTypeId == QMetaType::Void) // void return
            continue;

        const QByteArray &typeName = i == 0 ? QByteArray{method.typeName()}
                                            : method.parameterTypeName(argIndex);

        Shiboken::Conversions::SpecificConverter converter(typeName);
        if (converter) {
            if (!Shiboken::Conversions::pythonTypeIsObjectType(converter)) {
                if (metaTypeId == QMetaType::UnknownType) {
                    PyErr_Format(PyExc_TypeError, "Value types used on meta functions (including signals) need to be "
                                                  "registered on meta type: %s", typeName.data());
                    break;
                }
                methValues[i] = QVariant(QMetaType(metaTypeId));
            }
            methArgs[i] = methValues[i].data();
            if (i == 0) // Don't do this for return type
                continue;
            Shiboken::AutoDecRef obj(PySequence_GetItem(sequence.object(), argIndex));
            if (metaTypeId == QMetaType::QString) {
                QString tmp;
                converter.toCpp(obj, &tmp);
                methValues[i] = tmp;
            } else if (metaTypeId == PyObjectWrapper::metaTypeId()) {
                // Manual conversion, see PyObjectWrapper converter registration
                methValues[i] = QVariant::fromValue(PyObjectWrapper(obj.object()));
                methArgs[i] = methValues[i].data();
            } else {
                converter.toCpp(obj, methArgs[i]);
            }
        } else {
            const QByteArray description = i == 0 ? "return type "_ba : "argument type #"_ba + QByteArray::number(i);
            PyErr_Format(PyExc_TypeError, "Unknown %s used in call of meta function (that may be a signal): %s",
                         description.constData(), typeName.constData());
            return false;
        }
    }

    Py_BEGIN_ALLOW_THREADS
    QMetaObject::metacall(self, QMetaObject::InvokeMetaMethod, method.methodIndex(), methArgs.data());
    Py_END_ALLOW_THREADS
    if (retVal) {
        if (methArgs[0]) {
#ifdef Py_GIL_DISABLED
            static Shiboken::OnceValue<SbkConverter *> qVariantTypeConverterOnce;
            SbkConverter *qVariantTypeConverter = qVariantTypeConverterOnce.get([] {
                return Shiboken::Conversions::getConverter("QVariant");
            });
#else
            static SbkConverter *qVariantTypeConverter = Shiboken::Conversions::getConverter("QVariant");
#endif
            Q_ASSERT(qVariantTypeConverter);
            *retVal = Shiboken::Conversions::copyToPython(qVariantTypeConverter, &methValues[0]);
        } else {
            *retVal = Py_None;
            Py_INCREF(*retVal);
        }
    }

    return true;
}

} //namespace PySide::MetaFunction
