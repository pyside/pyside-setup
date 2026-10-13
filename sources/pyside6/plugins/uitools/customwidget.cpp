// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "customwidget.h"

#include <QtCore/qdebug.h>

#include <autodecref.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <sbkconverter.h>
#include <sbkpep.h>


// Part of the static plugin linked to the QtUiLoader Python module,
// allowing it to create a custom widget written in Python.
PyCustomWidget::PyCustomWidget(PyObject *objectType) :
    m_pyObject(objectType),
    m_name(QString::fromUtf8(PepType_GetFullyQualifiedNameStr(reinterpret_cast<PyTypeObject *>(objectType))))
{
}

bool PyCustomWidget::isContainer() const
{
    return false;
}

bool PyCustomWidget::isInitialized() const
{
    return m_initialized;
}

QIcon PyCustomWidget::icon() const
{
    return {};
}

QString PyCustomWidget::domXml() const
{
    return {};
}

QString PyCustomWidget::group() const
{
    return {};
}

QString PyCustomWidget::includeFile() const
{
    return {};
}

QString PyCustomWidget::name() const
{
    return m_name;
}

QString PyCustomWidget::toolTip() const
{
    return {};
}

QString PyCustomWidget::whatsThis() const
{
    return {};
}

// A copy of this code exists in PyDesignerCustomWidget::createWidget()
// (see sources/pyside6/PySide6/QtDesigner/qpydesignercustomwidgetcollection.cpp).
QWidget *PyCustomWidget::createWidget(QWidget *parent)
{
    // Create a python instance and return cpp object
    PyObject *pyParent = nullptr;
    bool unknownParent = false;
    if (parent != nullptr) {
        if (auto wrapper = Shiboken::BindingManager::instance().wrapperRef(parent)) {
            pyParent = wrapper.newRef();
        } else {
            static Shiboken::Conversions::SpecificConverter converter("QWidget*");
            pyParent = converter.toPython(&parent);
            unknownParent = true;
        }
    } else {
        Py_INCREF(Py_None);
        pyParent = Py_None;
    }

    Shiboken::AutoDecRef pyArgs(PyTuple_New(1));
    PyTuple_SetItem(pyArgs.object(), 0, pyParent); // tuple will keep pyParent reference

    // Call python constructor
    auto *obResult = PyObject_CallObject(m_pyObject, pyArgs);
    if (obResult == nullptr) {
        qWarning("Unable to create a Python custom widget of type \"%s\".",
                 qPrintable(m_name));
        PyErr_Print();
        return nullptr;
    }

    auto *result = reinterpret_cast<SbkObject *>(obResult);
    if (unknownParent) // if parent does not exist in python, transfer the ownership to cpp
        Shiboken::Object::releaseOwnership(result);
    else
        Shiboken::Object::setParent(pyParent, obResult);

    return reinterpret_cast<QWidget *>(Shiboken::Object::cppPointer(result, Py_TYPE(obResult)));
}

void PyCustomWidget::initialize(QDesignerFormEditorInterface *)
{
    m_initialized = true;
}
