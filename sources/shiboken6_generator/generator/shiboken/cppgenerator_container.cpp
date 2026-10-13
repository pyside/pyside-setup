// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "cppgenerator.h"
#include "generatorstrings.h"
#include <abstractmetalang.h>
#include "apiextractorresult.h"
#include "ctypenames.h"
#include "pymethoddefentry.h"
#include "containertypeentry.h"
#include "textstream.h"
#include "typedatabase.h"

#include <QtCore/qdebug.h>

#include <algorithm>

using namespace Qt::StringLiterals;

// Write a PyMethodDef entry, allowing for registering C++ functions
// under different names for Python.
static inline void writeMethod(TextStream &s, const QString &privateObjType,
                               const QString &cppName, const QString &pythonName = {})
{
    s << PyMethodDefEntry{pythonName.isEmpty() ? cppName : pythonName,
                          privateObjType + "::"_L1 + cppName,
                          PyMethodFlag::SingleObject, cppName}
        << ",\n";
}

static inline void writeNoArgsMethod(TextStream &s, const QString &privateObjType,
                                     const  QString &cppName, const QString &pythonName = {})
{
    s << PyMethodDefEntry{pythonName.isEmpty() ? cppName : pythonName,
                          privateObjType + "::"_L1 + cppName,
                          PyMethodFlag::NoArgs, cppName}
        << ",\n";
}

static void writeSlot(TextStream &s, const char *tpName, const char *value)
{
    s << '{' << tpName << ", reinterpret_cast<void *>(" << value << ")},\n";
}

static void writeSlot(TextStream &s, const QString &privateObjType,
                      const char *tpName, const char *methodName)
{
    s << '{' << tpName << ", reinterpret_cast<void *>(" << privateObjType
      << "::" << methodName << ")},\n";
}

// Write creation function from C++ reference, used by field accessors
// and getters which are within extern "C"

enum ContainerCreationFlag : std::uint8_t
{
    None = 0,
    Const = 0x1,
    Allocate = 0x2
};

Q_DECLARE_FLAGS(ContainerCreationFlags, ContainerCreationFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(ContainerCreationFlags)

static void writeContainerCreationFunc(TextStream &s,
                                       const QString &funcName,
                                       const QString &typeFName,
                                       const QString &containerSignature,
                                       ContainerCreationFlags flags = {})
{

    // creation function from C++ reference, used by field accessors
    // which are within extern "C"
    s << "extern \"C\" PyObject *" << funcName << '(';
    if (flags.testFlag(ContainerCreationFlag::Const))
        s << "const ";
    s << containerSignature << "* ct)\n{\n" << indent
        << "auto *container = PyObject_New(ShibokenContainer, " << typeFName << "());\n"
        << "auto *d = new ShibokenSequenceContainerPrivate<"
        << containerSignature << ">();\n";
    if (flags.testFlag(ContainerCreationFlag::Allocate)) {
        s << "d->m_list = new " << containerSignature << "(*ct);\n"
            << "d->m_ownsList = true;\n";
    } else if (flags.testFlag(ContainerCreationFlag::Const)) {
        s << "d->m_list = const_cast<" << containerSignature << " *>(ct);\n"
            << "d->m_const = true;\n";
    } else {
        s << "d->m_list = ct;\n";
    }
    s << "container->d = d;\n";
    s << "return reinterpret_cast<PyObject *>(container);\n" << outdent
        << "}\n\n";
}

static void writeConverterCheckFunc(TextStream &s,
                                    const QString &name,
                                    const QString &checkFunction,
                                    const QString &converterFunction)
{
    s << "extern \"C\" PythonToCppFunc " << name << "(PyObject *" << PYTHON_ARG << ")\n{\n"
      << indent << "if (" << checkFunction << '(' << PYTHON_ARG << "))\n" << indent
      << "return " << converterFunction << ";\n"
      << outdent << "return {};\n" << outdent << "}\n\n";
}

static void writeConverterFunctionHeader(TextStream &s,
                                         const QString &name,
                                         const QString &typeName)
{
    s << "extern \"C\" void " << name << "(PyObject *" << PYTHON_ARG << ", void *cppOut)\n{\n"
      << indent << "auto *d = ShibokenSequenceContainerPrivate<" << typeName
      << ">::get(" << PYTHON_ARG << ");\n";
}

static inline void writeConverterFunctionFooter(TextStream &s)
{
    s << outdent << "}\n\n";
}

// Generate template specialization of value converter helper
void CppGenerator::writeOpaqueContainerValueConverter(TextStream &s,
                                                      const AbstractMetaType &valueType) const
{
    // Generate template specialization of value converter helper unless it is already there
    const QString valueTypeName = valueType.cppSignature();
    const QString checkFunction = cpythonCheckFunction(valueType);

    s << "template <>\nstruct ShibokenContainerValueConverter<"
      << valueTypeName << ">\n{\n";
    // Type check
    s << indent << "static bool checkValue(PyObject *" << PYTHON_ARG << ")\n{\n"
        << indent << "return " << checkFunction;
    if (!checkFunction.contains(u'('))
        s << '(';
    s << PYTHON_ARG << ");\n"
        << outdent << "}\n\n";

    // C++ to Python
    const bool passByConstRef = valueType.indirectionsV().isEmpty()
        && !valueType.isCppPrimitive();
    s << "static PyObject *convertValueToPython(";
    if (passByConstRef)
        s << "const ";
    s << valueTypeName << ' ';
    if (passByConstRef)
        s << '&';
    s << CPP_ARG << ")\n{\n" << indent << "return ";
    writeToPythonConversion(s, valueType, nullptr, CPP_ARG);
    s << ";\n" << outdent << "}\n\n";

    // Python to C++
    s << "static std::optional<" << valueTypeName << "> convertValueToCpp(PyObject *"
        << PYTHON_ARG << ")\n{\n" << indent;
    s << PYTHON_TO_CPPCONVERSION_STRUCT << ' ' << PYTHON_TO_CPP_VAR << ";\n"
        << "if (!(";
    writeTypeCheck(s, valueType, PYTHON_ARG), isNumber(valueType.typeEntry());
    s << ")) {\n" << indent
        << "Shiboken::Errors::setWrongContainerType();\n"
        << "return {};\n" << outdent << "}\n";
    writePythonToCppTypeConversion(s, valueType, PYTHON_ARG, CPP_ARG, nullptr, {});
    s << "return " << CPP_ARG << ";\n" << outdent << "}\n" << outdent << "};\n\n";
}

// Generate code for a type wrapping a C++ container instantiation
CppGenerator::OpaqueContainerData
   CppGenerator::writeOpaqueContainerConverterFunctions(TextStream &s,
                                                        const AbstractMetaType &containerType,
                                                        QSet<AbstractMetaType> *valueTypes) const
{
    OpaqueContainerData result;
    const auto &valueType = containerType.instantiations().constFirst();
    const auto containerTypeEntry = std::static_pointer_cast<const ContainerTypeEntry>(containerType.typeEntry());
    result.name =
        containerTypeEntry->opaqueContainerName(containerType.instantiationCppSignatures());

    const auto cppSignature = containerType.cppSignature();
    s << "\n// Binding for " << cppSignature << "\n\n";

    if (!valueTypes->contains(valueType)) {
        valueTypes->insert(valueType);
        writeOpaqueContainerValueConverter(s, valueType);
    }

    const QString privateObjType = u"ShibokenSequenceContainerPrivate<"_s
        + cppSignature + u'>';

    // methods
    const QString &containerName = containerType.name();
    const bool isStdVector = containerName  == u"std::vector";
    result.hasQVariantConversion = usePySideExtensions() && containerName == "QList"_L1;
    const auto kind = containerTypeEntry->containerKind();
    const bool isFixed = kind == ContainerTypeEntry::SpanContainer || containerName == u"std::array";
    const QString methods = result.name + u"_methods"_s;
    const bool usePySide = CppGenerator::usePySideExtensions();
    s << (usePySide ? clangQtBeginSuppressWarnings : clangBeginSuppressWarnings)
        << "static PyMethodDef " << methods << "[] = {\n" << indent;
    if (!isFixed) {
        writeMethod(s, privateObjType, u"push_back"_s);
        writeMethod(s, privateObjType, u"push_back"_s, u"append"_s); // Qt convention
        writeNoArgsMethod(s, privateObjType, u"clear"_s);
        writeNoArgsMethod(s, privateObjType, u"pop_back"_s);
        writeNoArgsMethod(s, privateObjType, u"pop_back"_s, u"removeLast"_s); // Qt convention
        if (!isStdVector) {
            writeMethod(s, privateObjType, u"push_front"_s);
            writeMethod(s, privateObjType, u"push_front"_s, u"prepend"_s); // Qt convention
            writeNoArgsMethod(s, privateObjType, u"pop_front"_s);
            writeNoArgsMethod(s, privateObjType, u"pop_front"_s, u"removeFirst"_s); // Qt convention
        }
        writeMethod(s, privateObjType, u"reserve"_s); // SFINAE'd out for list
        writeNoArgsMethod(s, privateObjType, u"capacity"_s);
    }
    writeNoArgsMethod(s, privateObjType, u"data"_s);
    writeNoArgsMethod(s, privateObjType, u"constData"_s);
    s << "{nullptr, nullptr, 0, nullptr} // Sentinel\n"
        << outdent << "};\n"
        << (usePySide ? clangQtEndSuppressWarnings : clangEndSuppressWarnings) << '\n';

    // slots
    const QString slotsList = result.name + u"_slots"_s;
    s << "static PyType_Slot " << slotsList << "[] = {\n" << indent;
    writeSlot(s, privateObjType, "Py_tp_init", "tpInit");
    const auto *tpNew = containerTypeEntry->viewOn() == nullptr ? "tpNew" : "tpNewInvalid";
    writeSlot(s, privateObjType, "Py_tp_new", tpNew);
    writeSlot(s, privateObjType, "Py_tp_free", "tpFree");
    writeSlot(s, "Py_tp_dealloc", "Sbk_object_dealloc"); // FIXME?
    writeSlot(s, "Py_tp_methods", methods.toUtf8().constData());
    writeSlot(s, privateObjType, "Py_sq_ass_item", "sqSetItem");
    writeSlot(s, privateObjType, "Py_sq_length", "sqLen");
    writeSlot(s, privateObjType, "Py_sq_item", "sqGetItem");
    s << "{0, nullptr}\n" << outdent << "};\n\n";

    // spec
    const QString specName = result.name + u"_spec"_s;
    const QString name = TypeDatabase::instance()->defaultPackageName()
        + u'.' + result.name;
    s << "static PyType_Spec " << specName << " = {\n" << indent
        << "\"" << name.count(u'.') << ':' << name << "\",\n"
        << "sizeof(ShibokenContainer),\n0,\nPy_TPFLAGS_DEFAULT,\n"
        <<  slotsList << outdent << "\n};\n\n";

    // type creation function that sets a key in the type dict.
    const QString typeCreationFName =  u"create"_s + result.name + u"Type"_s;
    s << "static inline PyTypeObject *" << typeCreationFName << "()\n{\n" << indent
        << "auto *result = SbkType_FromSpec(&"
        << specName <<  ");\nPy_INCREF(Py_True);\n"
        << "Shiboken::AutoDecRef tpDict(PepType_GetDict(result));\n"
        << "PyDict_SetItem(tpDict.object(), "
           "Shiboken::PyMagicName::opaque_container(), Py_True);\n"
        << "return result;\n" << outdent << "}\n\n";

    // _TypeF() function
    const QString typeFName =  result.name + u"_TypeF"_s;
    s << "static PyTypeObject *" << typeFName << "()\n{\n" << indent
        << "#ifdef Py_GIL_DISABLED\n"
        << "static Shiboken::OnceValue<PyTypeObject *> type;\n"
        << "return type.get(" << typeCreationFName << ");\n"
        << "#else\n"
        << "static PyTypeObject *type = " << typeCreationFName
        << "();\nreturn type;\n"
        << "#endif\n" << outdent << "}\n\n";

    // creation functions from C++ references
    ContainerCreationFlags flags;
    if (kind == ContainerTypeEntry::SpanContainer)
        flags.setFlag(ContainerCreationFlag::Allocate);

    writeContainerCreationFunc(s, u"create"_s + result.name, typeFName,
                               containerType.cppSignature(), flags);
    flags.setFlag(ContainerCreationFlag::Const);
    writeContainerCreationFunc(s, u"createConst"_s + result.name, typeFName,
                               containerType.cppSignature(), flags);

    // Check function
    result.checkFunctionName = result.name + u"_Check"_s;
    s << "extern \"C\" int " << result.checkFunctionName << "(PyObject *" << PYTHON_ARG
        << ")\n{\n" << indent << "return " << PYTHON_ARG << " != nullptr && "
        << PYTHON_ARG << " != Py_None && " << PYTHON_ARG << "->ob_type == "
        << typeFName << "();\n" << outdent << "}\n\n";

    // SBK converter Python to C++
    result.pythonToConverterFunctionName = u"PythonToCpp"_s + result.name;
    writeConverterFunctionHeader(s, result.pythonToConverterFunctionName, cppSignature);
    s << "*reinterpret_cast<" << cppSignature << "**>(cppOut) = d->m_list;\n";
    writeConverterFunctionFooter(s);

    // SBK check function for converting Python to C++ that returns the converter
    result.converterCheckFunctionName = u"is"_s + result.name + u"PythonToCppConvertible"_s;
    writeConverterCheckFunc(s, result.converterCheckFunctionName, result.checkFunctionName,
                            result.pythonToConverterFunctionName);

    TextStream registrationStr(&result.registrationCode, TextStream::Language::Cpp);
    registrationStr << "pyType = " << typeFName << "();\n"
        << "Py_XINCREF(reinterpret_cast<PyObject *>(pyType));\n"
        << "PepModule_AddType(module, pyType);\n";

    if (!result.hasQVariantConversion)
        return result;

    // PYSIDE-2820: Add a conversion to QVariant for QML. Note QVariant::fromValue()
    // will work for all list types, but we restrict it to registered meta types for QML.
    QString pythonToQVariantConverterFunctionName = "PythonToQVariant"_L1 + result.name;
    QString pythonToQVariantCheckFunctionName = "is"_L1 + result.name +
                                                "PythonToQVariantConvertible"_L1;
    writeConverterFunctionHeader(s, pythonToQVariantConverterFunctionName, cppSignature);
    s << "*reinterpret_cast<QVariant *>(cppOut) = QVariant::fromValue(*d->m_list);\n";
    writeConverterFunctionFooter(s);
    writeConverterCheckFunc(s, pythonToQVariantCheckFunctionName,
                            result.checkFunctionName,
                            pythonToQVariantConverterFunctionName);
    registrationStr << "if constexpr (QMetaTypeId2<" << valueType.cppSignature()
        << ">::Defined) {\n" << indent
        << "Shiboken::Conversions::prependPythonToCppValueConversion(qVariantConverter,\n    "
        << pythonToQVariantConverterFunctionName << ", "
        << pythonToQVariantCheckFunctionName << ");\n" << outdent << "}\n";

    return result;
}
