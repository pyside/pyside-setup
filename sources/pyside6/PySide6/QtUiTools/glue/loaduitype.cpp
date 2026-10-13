// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:execute-external-code

#include "loaduitype.h"

#include <pysideutils.h>

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qoperatingsystemversion.h>
#include <QtCore/qprocess.h>
#include <QtCore/qxmlstream.h>

#include <autodecref.h>
#ifdef Py_GIL_DISABLED
#  include <sbkheldlocks.h>
#endif

#include <optional>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

#ifdef Q_OS_IOS

PyObject *loadUiType(PyObject *obFileName, PyObject *obPathSearch)
{
    Q_UNUSED(obFileName);
    Q_UNUSED(obPathSearch);
    // QProcess is not available on iOS; loadUiType() cannot be supported.
    qCritical("loadUiType: Not supported on iOS since QProcess is unavailable.");
    Py_RETURN_NONE;
}

#else

static QString getUicBinary()
{
    QString binary = u"pyside6-uic"_s;
    if constexpr (QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows)
        binary += ".exe"_L1;

    QString result = PySide::sysExecutable();
    if (auto pos = result.lastIndexOf(u'/'); pos != -1) {
        ++pos;
        result.replace(pos, result.size() - pos, binary);
        if (!QFileInfo::exists(result))
            result = binary;
    } else {
        result = binary;
    }
    return result;
}

// Problem: The generated Python file doesn't have the Qt Base class information.
// Solution: Use the XML file.

struct XmlClassNames
{
    QString className;
    QString baseClassName;
};

static std::optional<XmlClassNames> getXmlClassNames(const QString &uiFileName)
{
    QFile uiFile(uiFileName);
    if (!uiFile.open(QIODevice::ReadOnly)) {
        qCritical().noquote().nospace()
            << "loadUiType: Cannot open " << QDir::toNativeSeparators(uiFileName)
            << ": " << uiFile.errorString();
        return std::nullopt;
    }

    // This will look for the first <widget> tag, e.g.:
    //      <widget class="QWidget" name="ThemeWidgetForm">
    // and then extract the information from "class", and "name",
    // to get the baseClassName and className respectively
    QString className;
    QString baseClassName;
    QXmlStreamReader reader(&uiFile);
    while (!reader.atEnd() && baseClassName.isEmpty() && className.isEmpty()) {
        auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == u"widget") {
            baseClassName = reader.attributes().value("class").toString();
            className = reader.attributes().value("name").toString();
        }
    }

    if (reader.hasError()) {
        qCritical().noquote().nospace()
            << "loadUiType: An error occurred when parsing the UI file "
               "while looking for the class info: " << reader.errorString();
        return std::nullopt;
    }

    if (baseClassName.isEmpty() || className.isEmpty()) {
        qCritical("loadUiType: The class name elements could not be found.");
        return std::nullopt;
    }

    if (!PySide::isUiClassName(baseClassName)) {
        qCritical("loadUiType: The base class name \"%s\" is not a valid identifier.",
                  qPrintable(baseClassName));
        return std::nullopt;
    }

    if (!PySide::isUiClassName(className)) {
        qCritical("loadUiType: The class name \"%s\" is not a valid identifier.",
                  qPrintable(className));
        return std::nullopt;
    }

    return XmlClassNames{className, baseClassName};
}

PyObject *loadUiType(PyObject *obFileName, PyObject *obPathSearch)
{
    // 1. Generate the Python code from the UI file
    const bool pathSearch = obPathSearch != nullptr && PyBool_Check(obPathSearch) && Py_IsTrue(obPathSearch);
    const QString uiFileName = PySide::pyUnicodeToQString(obFileName);
    if (uiFileName.isEmpty()) {
        qCritical("loadUiType: Error converting the UI filename");
        Py_RETURN_NONE;
    }

    if (!QFileInfo::exists(uiFileName)) {
        qCritical().noquote() << "loadUiType: File" << QDir::toNativeSeparators(uiFileName)
                              << "does not exist";
        Py_RETURN_NONE;
    }

#ifdef Py_GIL_DISABLED
    static Shiboken::OnceValue<const QString *> uicBinOnce;
    const QString &uicBin = *uicBinOnce.get([] { return new QString(getUicBinary()); });
#else
    static const QString uicBin = getUicBinary();
#endif
    if (!uicBin.contains(u'/')) {
        if (!pathSearch) {
            qCritical("loadUiType: %s could not be found in the standard location.",
                      qPrintable(uicBin));
            Py_RETURN_NONE;
        }
        qWarning("loadUiType(): \"%s\" could not be found in the Python installation, "
                 "falling back to using a relative path. This poses a security risk. "
                 "Please contact the application vendor to fix the issue.", qPrintable(uicBin));
    }

    QProcess uicProcess;
    uicProcess.start(uicBin, {uiFileName});
    if (!uicProcess.waitForStarted()) {
        qCritical().noquote().nospace()
            << "loadUiType(): Cannot run \"" << QDir::toNativeSeparators(uicBin)
            << "\": " << uicProcess.errorString() << " - Check if 'pyside6-uic' is in PATH";
        Py_RETURN_NONE;
    }

    if (!uicProcess.waitForFinished()
        || uicProcess.exitStatus() != QProcess::NormalExit
        || uicProcess.exitCode() != 0) {
        qCritical().noquote().nospace() << '"' << QDir::toNativeSeparators(uicBin) << "\" failed: "
            << uicProcess.errorString() << " - Exit status " << uicProcess.exitStatus()
            << " (" << uicProcess.exitCode() << ")\n";
        Py_RETURN_NONE;
    }

    const QByteArray uiFileContent = uicProcess.readAllStandardOutput();
    const QByteArray errorOutput = uicProcess.readAllStandardError();

    if (!errorOutput.isEmpty()) {
        qCritical().noquote().nospace() << "loadUiType: \"" << QDir::toNativeSeparators(uicBin)
                                        << "\" failed: " << errorOutput;
        Py_RETURN_NONE;
    }

    // 2. Obtain the 'classname' and the Qt base class.
    auto classNamesOpt = getXmlClassNames(uiFileName);
    if (!classNamesOpt.has_value())
        Py_RETURN_NONE;

    QByteArray pyClassName("Ui_" + classNamesOpt->className.toUtf8());

    // 3. exec() the code so the class exists in the context: exec(uiFileContent)
    // The context of PyRun_SimpleString is __main__.
    // 'Py_file_input' is the equivalent to using exec(), since it will execute
    // the code, without returning anything.
    Shiboken::AutoDecRef codeUi(Py_CompileString(uiFileContent.constData(), "<stdin>",
                                                 Py_file_input));
    if (codeUi.isNull()) {
        qCritical("loadUiType: Error while compiling the generated Python file");
        Py_RETURN_NONE;
    }

    Shiboken::AutoDecRef loc(PyDict_New());
    PyObject *uiObj = PyEval_EvalCode(codeUi, loc.object(), loc.object());

    if (uiObj == nullptr) {
        qCritical("loadUiType: Error while running exec() on the generated code");
        Py_RETURN_NONE;
    }

    // 4. eval() the name of the class on a variable to return
    // 'Py_eval_input' is the equivalent to using eval(), since it will just
    // evaluate an expression.
    Shiboken::AutoDecRef codeClass(Py_CompileString(pyClassName.constData(), "<stdin>",
                                                    Py_eval_input));
    if (codeClass.isNull()) {
        qCritical() << "Error while compiling the Python class";
        Py_RETURN_NONE;
    }

    Shiboken::AutoDecRef codeBaseClass(Py_CompileString(classNamesOpt->baseClassName.toUtf8().constData(),
                                                        "<stdin>", Py_eval_input));
    if (codeBaseClass.isNull()) {
        qCritical("loadUiType: Error while compiling the base class");
        Py_RETURN_NONE;
    }

    PyObject *classObj = PyEval_EvalCode(codeClass, loc.object(), loc.object());
    PyObject *baseClassObj = PyEval_EvalCode(codeBaseClass, loc.object(), loc.object());

    PyObject *result = PyTuple_Pack(2, classObj, baseClassObj);
    if (result == nullptr) {
        qCritical("loadUiType: Error while creating the return Tuple");
        Py_RETURN_NONE;
    }
    return result;
}

#endif // Q_OS_IOS

QT_END_NAMESPACE
