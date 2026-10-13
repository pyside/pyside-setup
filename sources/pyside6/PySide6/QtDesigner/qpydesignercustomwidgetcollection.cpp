// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include <qpydesignerextensions.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QVariant>

#include <sbkpep.h>
#include <pep384ext.h>
#include <autodecref.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <sbkconverter.h>
#include <sbkstring.h>

#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

QT_BEGIN_NAMESPACE

static QString pyStringToQString(PyObject *s)
{
    Py_ssize_t size{};
    const char *utf8 = PyUnicode_AsUTF8AndSize(s, &size);
    return utf8 ? QString::fromUtf8(utf8, size) : QString{};
}

// Return a string from keyword argument dict
static QString kwdString(PyObject *kwds, PyObject *key)
{
    QString result;
    if (PyDict_Contains(kwds, key)) {
        if (auto value = PyDict_GetItem(kwds, key))
            result = pyStringToQString(value);
    }
    return result;
}

// Return a bool from keyword argument dict
static bool kwdBool(PyObject *kwds, PyObject *key)
{
    bool result = false;
    if (PyDict_Contains(kwds, key)) {
        if (auto value = PyDict_GetItem(kwds, key))
            result = PyObject_IsTrue(value);
    }
    return result;
}

// PyDesignerCustomWidget: A custom widget registered by type
// (similar to what is done by QUiLoader.registerCustomWidget()).
class PyDesignerCustomWidget : public QDesignerCustomWidgetInterface
{
public:
    explicit PyDesignerCustomWidget(PyObject *pyTypeObject) :
        m_name(QString::fromUtf8(PepExt_TypeGetQualName(reinterpret_cast<PyTypeObject *>(pyTypeObject)))),
        m_pyTypeObject(pyTypeObject)
    {
    }

    QString name() const override { return m_name; }
    QString group() const override { return m_group; }
    QString toolTip() const override { return m_toolTip; }
    QString whatsThis() const  override { return toolTip(); }
    QString includeFile() const override { return m_includeFile; }
    QIcon icon() const override { return m_icon; }
    bool isContainer() const override { return m_container; }

    QWidget *createWidget(QWidget *parent) override;

    bool isInitialized() const  override { return m_core != nullptr; }
    void initialize(QDesignerFormEditorInterface *core) override;

    QString domXml() const override { return m_domXml; }

    void setGroup(const QString &group) { m_group = group; }
    void setToolTip(const QString &toolTip) { m_toolTip = toolTip; }
    void setIncludeFile(const QString &includeFile) { m_includeFile = includeFile; }
    void setIcon(const QIcon &icon) { m_icon = icon; }
    void setDomXml(const QString &domXml) { m_domXml = domXml; }
    void setContainer(bool container) { m_container = container; }

private:
    QDesignerFormEditorInterface *m_core = nullptr;
    QString m_name;
    QString m_group;
    QString m_toolTip;
    QString m_includeFile;
    QIcon m_icon;
    QString m_domXml;
    PyObject *m_pyTypeObject = nullptr;
    bool m_container = false;
};

QWidget *PyDesignerCustomWidget::createWidget(QWidget *parent)
{
    // This is a copy of the similar function used for QUiLoader
    // (see sources/pyside6/plugins/uitools/customwidget.cpp)
    // Create a python instance and return cpp object
    PyObject *pyParent = nullptr;
    bool unknownParent = false;
    if (parent) {
        if (auto wrapper = Shiboken::BindingManager::instance().wrapperRef(parent)) {
            pyParent = wrapper.newRef();
        } else {
#ifdef Py_GIL_DISABLED
            static Shiboken::OnceValue<Shiboken::Conversions::SpecificConverter *> converterOnce;
            auto &converter = *converterOnce.get([] {
                return new Shiboken::Conversions::SpecificConverter("QWidget*");
            });
#else
            static Shiboken::Conversions::SpecificConverter converter("QWidget*");
#endif
            pyParent = converter.toPython(&parent);
            unknownParent = true;
        }
    } else {
        Py_INCREF(Py_None);
        pyParent = Py_None;
    }

    Shiboken::AutoDecRef pyArgs(PyTuple_New(1));
    PyTuple_SetItem(pyArgs, 0, pyParent); // tuple will keep pyParent reference

    // Call python constructor
    auto *obResult = PyObject_CallObject(m_pyTypeObject, pyArgs);
    if (obResult == nullptr) {
        qWarning("Unable to create a Python custom widget of type \"%s\".", qPrintable(m_name));
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

void PyDesignerCustomWidget::initialize(QDesignerFormEditorInterface *core)
{
    m_core = core;
}

// QPyDesignerCustomWidgetCollection: A QDesignerCustomWidgetCollectionInterface
// implementation that is instantiated as a singleton and stored as a dynamic
// property of QCoreApplication. The PySide Designer plugin retrieves it from
// there. Provides static convenience functions for registering types
// or adding QDesignerCustomWidgetInterface instances.

static QPyDesignerCustomWidgetCollection *collectionInstance = nullptr;

static const char propertyName[] =  "__qt_PySideCustomWidgetCollection";

static void cleanup()
{
    delete collectionInstance;
    collectionInstance = nullptr;
}

QPyDesignerCustomWidgetCollection::QPyDesignerCustomWidgetCollection() = default;

QPyDesignerCustomWidgetCollection::~QPyDesignerCustomWidgetCollection()
{
    qDeleteAll(m_customWidgets);
}

QList<QDesignerCustomWidgetInterface *> QPyDesignerCustomWidgetCollection::customWidgets() const
{
    return m_customWidgets;
}

QPyDesignerCustomWidgetCollection *QPyDesignerCustomWidgetCollection::instance()
{
    if (collectionInstance == nullptr) {
        collectionInstance = new QPyDesignerCustomWidgetCollection();
        if (auto coreApp = QCoreApplication::instance()) {
            QDesignerCustomWidgetCollectionInterface *c = collectionInstance;
            coreApp->setProperty(propertyName, QVariant::fromValue<void *>(c));
            qAddPostRoutine(cleanup);
        } else {
            qWarning("%s: Cannot find QCoreApplication instance.", Q_FUNC_INFO);
        }
    }
    return collectionInstance;
}

// Register a custom widget by type and optional keyword arguments providing
// the parameters of QDesignerCustomWidgetInterface.
bool QPyDesignerCustomWidgetCollection::_registerCustomWidgetHelper(PyObject *typeArg, PyObject *kwds)
{
    if (!PyType_Check(typeArg)) {
        PyErr_SetString(PyExc_TypeError, "registerCustomWidget() requires a type argument.");
        return false;
    }

    auto pyCustomWidget = new PyDesignerCustomWidget(typeArg);

#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString xmlKey("xml");
#else
    static PyObject *xmlKey = Shiboken::String::createStaticString("xml");
#endif
    pyCustomWidget->setDomXml(kwdString(kwds, xmlKey));
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString toolTipKey("tool_tip");
#else
    static PyObject *toolTipKey = Shiboken::String::createStaticString("tool_tip");
#endif
    pyCustomWidget->setToolTip(kwdString(kwds, toolTipKey));
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString groupKey("group");
#else
    static PyObject *groupKey = Shiboken::String::createStaticString("group");
#endif
    pyCustomWidget->setGroup(kwdString(kwds, groupKey));
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString moduleKey("module");
#else
    static PyObject *moduleKey = Shiboken::String::createStaticString("module");
#endif
    pyCustomWidget->setIncludeFile(kwdString(kwds, moduleKey));
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString containerKey("container");
#else
    static PyObject *containerKey = Shiboken::String::createStaticString("container");
#endif
    pyCustomWidget->setContainer(kwdBool(kwds, containerKey));
#ifdef Py_GIL_DISABLED
    static Shiboken::OnceString iconKey("icon");
#else
    static PyObject *iconKey = Shiboken::String::createStaticString("icon");
#endif
    const QString iconPath = kwdString(kwds, iconKey);
    if (!iconPath.isEmpty()) {
        QIcon icon(iconPath);
        if (icon.availableSizes().isEmpty())
            qWarning("%s: Cannot load icon from '%s'.", __FUNCTION__, qPrintable(iconPath));
        else
            pyCustomWidget->setIcon(icon);
    }

    addCustomWidget(pyCustomWidget);
    return true;
}

void QPyDesignerCustomWidgetCollection::addCustomWidget(QDesignerCustomWidgetInterface *c)
{
    instance()->m_customWidgets.append(c);
}

QT_END_NAMESPACE
