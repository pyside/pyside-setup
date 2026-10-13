// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "pysidequickregistertype.h"

#include <pyside.h>
#include <pysideqmlregistertype.h>
#include <pyside_p.h>
#include <sbkpython.h>
#include <sbkconverter.h>
#include <sbkpep.h>
#include <sbktypetupleref.h>

#include <QtQuick/QQuickPaintedItem>
#include <QtQuick/QQuickTextDocument>

#if QT_CONFIG(opengl) || QT_CONFIG(opengles2) || QT_CONFIG(opengles3)
#  include <QtQuick/QQuickFramebufferObject>
#endif

bool pyTypeObjectInheritsFromClass(PyTypeObject *pyObjType, const char *classPtrName)
{
    PyTypeObject *classPyType = Shiboken::Conversions::getPythonTypeObject(classPtrName);
    PepMroRef mroRef(pyObjType);
    bool isDerived = PySequence_Contains(mroRef.object(),
                                         reinterpret_cast<PyObject *>(classPyType));
    return isDerived;
}

template <class WrappedClass>
bool registerTypeIfInheritsFromClass(const char *classPtrName,
                                     PyTypeObject *typeToRegister,
                                     QQmlPrivate::RegisterTypeAndRevisions *type)
{
    if (!pyTypeObjectInheritsFromClass(typeToRegister, classPtrName))
        return false;
    type->parserStatusCast =
            QQmlPrivate::StaticCastSelector<WrappedClass, QQmlParserStatus>::cast();
    type->valueSourceCast =
            QQmlPrivate::StaticCastSelector<WrappedClass, QQmlPropertyValueSource>::cast();
    type->valueInterceptorCast =
            QQmlPrivate::StaticCastSelector<WrappedClass, QQmlPropertyValueInterceptor>::cast();
    return true;
}

bool quickRegisterType(PyObject *pyObj, QQmlPrivate::RegisterTypeAndRevisions *type)
{
    using namespace Shiboken;

    PyTypeObject *pyObjType = reinterpret_cast<PyTypeObject *>(pyObj);
    PyTypeObject *qQuickItemPyType =
            Shiboken::Conversions::getPythonTypeObject("QQuickItem*");
    PepMroRef mroRef(pyObjType);
    bool isQuickItem = PySequence_Contains(mroRef.object(),
                                           reinterpret_cast<PyObject *>(qQuickItemPyType));

    // Register only classes that inherit QQuickItem or its children.
    if (!isQuickItem)
        return false;

    return registerTypeIfInheritsFromClass<QQuickPaintedItem>("QQuickPaintedItem*",
                                                              pyObjType, type)
#if QT_CONFIG(opengl) || QT_CONFIG(opengles2) || QT_CONFIG(opengles3)
        || registerTypeIfInheritsFromClass<QQuickFramebufferObject>("QQuickFramebufferObject*",
                                                                    pyObjType, type)
#endif
        || registerTypeIfInheritsFromClass<QQuickItem>("QQuickItem*",
                                                       pyObjType, type);
}

void PySide::initQuickSupport(PyObject *module)
{
    Q_UNUSED(module);
    // We need to manually register a pointer version of these types in order for them to be used as property types.
    qRegisterMetaType<QQuickPaintedItem*>("QQuickPaintedItem*");
#if QT_CONFIG(opengl) || QT_CONFIG(opengles2) || QT_CONFIG(opengles3)
    qRegisterMetaType<QQuickFramebufferObject*>("QQuickFramebufferObject*");
#endif
    qRegisterMetaType<QQuickItem*>("QQuickItem*");
    qRegisterMetaType<QQuickTextDocument*>("QQuickTextDocument*");

    Qml::setQuickRegisterItemFunction(quickRegisterType);
}
