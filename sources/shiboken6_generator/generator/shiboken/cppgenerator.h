// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#ifndef CPPGENERATOR_H
#define CPPGENERATOR_H

#include "shibokengenerator.h"
#include "include.h"
#include "modifications_typedefs.h"
#include "pymethoddefentry.h"

#include <QtCore/qflags.h>
#include <QtCore/qset.h>
#include <QtCore/qhash.h>

#include <memory>
#include <utility>

class OverloadDataNode;
class OverloadDataRootNode;
struct PyMethodDefEntry;

/**
 *   The CppGenerator generate the implementations of C++ bindings classes.
 */
class CppGenerator : public ShibokenGenerator
{
public:
    enum class ErrorReturn : std::uint8_t {
        Default, // "{}"
        Zero,
        MinusOne,
        NullPtr,
        Void
    };

    enum CppSelfDefinitionFlag : std::uint8_t {
        HasStaticOverload = 0x1,
        HasClassMethodOverload = 0x2,
        CppSelfAsReference = 0x4,
        MaybeUnused = 0x8
    };
    Q_DECLARE_FLAGS(CppSelfDefinitionFlags, CppSelfDefinitionFlag)

    CppGenerator();

    const char *name() const override { return "Source generator"; }

    enum class NamedArgumentFlag : std::uint8_t {
        UsePyArgs               = 0x1, // List of arguments instead of single argument
        HasDefaultArguments     = 0x2,
        ForceKeywordArguments   = 0x4, // See forceQObjectNamedArguments()
        KeywordArgumentsMask    = HasDefaultArguments | ForceKeywordArguments,
        QObjectConstructor      = 0x8
    };
    Q_DECLARE_FLAGS(NamedArgumentFlags, NamedArgumentFlag)

protected:
    QString fileNameForContext(const GeneratorContext &context) const override;
    void generateClass(TextStream &s, const QString &targetDir,
                       const GeneratorContext &classContext,
                       QList<GeneratorContext> *contexts) override;
    void generateSmartPointerClass(TextStream &s, const QString &targetDir,
                                   const GeneratorContext &classContext) override;
    bool finishGeneration() override;

private:
    struct VirtualMethodReturn
    {
        QString statement;
        bool needsReference = false;
    };

    void generateIncludes(TextStream &s, const GeneratorContext &classContext,
                          const IncludeGroupList &includes = {},
                          const AbstractMetaClassCList &innerClasses = {}) const;
    static void writeInitFuncCall(TextStream &callStr,
                                  const QString &functionName,
                                  const TypeEntryCPtr &enclosingEntry,
                                  const QString &pythonName);
    void writeLazyTypeCreationFunc(TextStream &s, const QString &funcName) const;
    static void writeCacheResetNative(TextStream &s, const GeneratorContext &classContext);
    void writeConstructorNative(TextStream &s, const GeneratorContext &classContext,
                                const AbstractMetaFunctionCPtr &func) const;
    static void writeDestructorNative(TextStream &s, const GeneratorContext &classContext,
                                      const AbstractMetaFunctionCList &virtualFuncs);

    QString getVirtualFunctionReturnTypeName(const AbstractMetaFunctionCPtr &func) const;
    static std::pair<QString, QChar> virtualMethodNativeArg(const AbstractMetaFunctionCPtr &func,
                               const AbstractMetaArgument &arg);
    static void writeVirtualMethodNativeVectorCallArgs(TextStream &s,
                                                       const AbstractMetaFunctionCPtr &func,
                                                       const AbstractMetaArgumentList &arguments,
                                                       const QList<int> &invalidateArgs);
    static void writeVirtualMethodNativeArgs(TextStream &s,
                                             const AbstractMetaFunctionCPtr &func,
                                             const AbstractMetaArgumentList &arguments,
                                             const QList<int> &invalidateArgs);
    void writeVirtualMethodNative(TextStream &s,
                                  const AbstractMetaFunctionCPtr &func,
                                  qsizetype cacheIndex) const;
    void writeVirtualMethodPythonOverride(TextStream &s,
                                          const AbstractMetaFunctionCPtr &func,
                                          const CodeSnipList &snips,
                                          const VirtualMethodReturn &returnStatement) const;
    void writeUserAddedPythonOverride(TextStream &s,
                                      const AbstractMetaFunctionCPtr &func) const;
    void writeVirtualMethodCppCall(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                   const QString &funcName, const QList<CodeSnip> &snips,
                                   const AbstractMetaArgument *lastArg, const TypeEntryCPtr &retType,
                                   const QString &returnStatement,
                                   bool ownsGil, bool hasGilVar) const;

    static VirtualMethodReturn virtualMethodReturn(const ApiExtractorResult &api,
                                                   const AbstractMetaFunctionCPtr &func,
                                                   const FunctionModificationList &functionModifications);
    void writeMetaObjectMethod(TextStream &s, const GeneratorContext &classContext) const;
    static void writeMetaCast(TextStream &s, const GeneratorContext &classContext);

    void writeEnumConverterFunctions(TextStream &s, const AbstractMetaEnum &metaEnum) const;
    static void writePointerToPythonConverter(TextStream &c, const GeneratorContext &context,
                                              const QString &cpythonType);
    void writeConverterFunctions(TextStream &s, const AbstractMetaClassCPtr &metaClass,
                                 const GeneratorContext &classContext) const;
    void writeCustomConverterFunctions(TextStream &s,
                                       const CustomConversionPtr &customConversion) const;
    void writeConverterRegister(TextStream &s, const AbstractMetaClassCPtr &metaClass,
                                const GeneratorContext &classContext) const;
    static void writeCustomConverterRegister(TextStream &s,
                                             const CustomConversionPtr &customConversion,
                                             const QString &converterVar);
    static void writeTemplateCustomConverterRegister(TextStream &s,
                                                     const AbstractMetaType &type,
                                                     QString converter = {});

    void writeContainerConverterFunctions(TextStream &s,
                                          const AbstractMetaType &containerType) const;

    struct OpaqueContainerData
    {
        QString name;
        QString checkFunctionName;
        QString converterCheckFunctionName;
        QString pythonToConverterFunctionName;
        QString registrationCode;
        bool hasQVariantConversion = false;
    };
    using OpaqueContainerTypeHash = QHash<AbstractMetaType, OpaqueContainerData>;

    OpaqueContainerData
        writeOpaqueContainerConverterFunctions(TextStream &s,
                                               const AbstractMetaType &containerType,
                                               QSet<AbstractMetaType> *valueTypes) const;
    void writeOpaqueContainerValueConverter(TextStream &s,
                                            const AbstractMetaType &valueType) const;

    void writeSmartPointerConverterFunctions(TextStream &s,
                                             const AbstractMetaType &smartPointerType) const;

    static void writeMethodWrapperPreamble(TextStream &s,
                                           const OverloadData &overloadData,
                                           const GeneratorContext &context,
                                           ErrorReturn errorReturn = ErrorReturn::Default);
    static void writeConstructorWrapperPreamble(TextStream &s,
                                                const OverloadData &overloadData,
                                                NamedArgumentFlags flags,
                                                const GeneratorContext &context,
                                                ErrorReturn errorReturn);
    static void writeCommonMethodWrapperPreamble(TextStream &s,
                                                 const OverloadData &overloadData,
                                                 const GeneratorContext &context,
                                                 bool initPythonArguments,
                                                 ErrorReturn errorReturn);
    void writeConstructorWrapper(TextStream &s,
                                 const OverloadData &overloadData,
                                 const GeneratorContext &classContext) const;
    static void writeConstructorDummy(TextStream &s, const GeneratorContext &classContext,
                                      const char *errorMessageFunc);
    void writeMethodWrapper(TextStream &s, const OverloadData &overloadData,
                            const GeneratorContext &classContext) const;
    void writeMethodWrapper(TextStream &s, TextStream &definitionStream,
                            TextStream &signatureStream,
                            const AbstractMetaFunctionCList &overloads,
                            const GeneratorContext &classContext) const;
    static void writeArgumentsInitializer(TextStream &s, const OverloadData &overloadData,
                                          const GeneratorContext &classContext,
                                          ErrorReturn errorReturn = ErrorReturn::Default);
    static void writeCppSelfConversion(TextStream &s,
                                       const GeneratorContext &context,
                                       const QString &className,
                                       bool useWrapperClass);
    static void writeSmartPointerCppSelfConversion(TextStream &s,
                                                   const GeneratorContext &context);

    static void writeCppSelfVarDef(TextStream &s, CppSelfDefinitionFlags flags = {});
    static void writeSmartPointerCppSelfDefinition(TextStream &s,
                                                   const GeneratorContext &,
                                                   ErrorReturn errorReturn = ErrorReturn::Default,
                                                   CppSelfDefinitionFlags flags = {});
    static void writeCppSelfDefinition(TextStream &s,
                                       const AbstractMetaFunctionCPtr &func,
                                       const GeneratorContext &context,
                                       ErrorReturn errorReturn = ErrorReturn::Default,
                                       CppSelfDefinitionFlags flags = {});
    static void writeCppSelfDefinition(TextStream &s,
                                       const GeneratorContext &context,
                                       ErrorReturn errorReturn = ErrorReturn::Default,
                                       CppSelfDefinitionFlags flags = {});

    static void writeErrorSection(TextStream &s,
                                  const OverloadData &overloadData,
                                  ErrorReturn errorReturn);

    static QString returnErrorWrongArguments(const OverloadData &overloadData,
                                             const GeneratorContext &context,
                                             ErrorReturn errorReturn,
                                             bool hasErrInfo = false);

    static void writeFunctionReturnErrorCheckSection(TextStream &s,
                                                     ErrorReturn errorReturn,
                                                     bool hasReturnValue = true);

    /// Writes the check section for the validity of wrapped C++ objects.
    static void writeInvalidPyObjectCheck(TextStream &s, const QString &pyObj,
                                          ErrorReturn errorReturn);

    static void writeTypeCheck(TextStream &s, const AbstractMetaType &argType,
                               const QString &argumentName,
                               bool isNumber = false, bool rejectNull = false);
    static void writeTypeCheck(TextStream &s, const QString &customType,
                        const QString &argumentName);
    static void writeTypeCheck(TextStream& s, const std::shared_ptr<OverloadDataNode> &overloadData,
                        const QString &argumentName);

    static void replacePolymorphicIdPlaceHolders(const AbstractMetaClassCPtr &metaClass,
                                                 QString *id);
    static void writeTypeDiscoveryFunction(TextStream &s,
                                           const AbstractMetaClassCPtr &metaClass);

    static void writeSetattroDefinition(TextStream &s, const AbstractMetaClassCPtr &metaClass);
    static void writeSetattroDefaultReturn(TextStream &s);
    static void writeSmartPointerSetattroFunction(TextStream &s,
                                                  const GeneratorContext &context);
    void writeSetattroFunction(TextStream &s,
                               AttroCheck attroCheck,
                               const GeneratorContext &context) const;
    static void writeGetattroDefinition(TextStream &s, const AbstractMetaClassCPtr &metaClass);
    static void writeSmartPointerGetattroFunction(TextStream &s,
                                                  const GeneratorContext &context,
                                                  const BoolCastFunctionOptional &boolCast);
    void writeGetattroFunction(TextStream &s, AttroCheck attroCheck,
                               const GeneratorContext &context) const;
    QString qObjectGetAttroFunction() const;

    static void writeNbBoolFunction(const GeneratorContext &context,
                                    const BoolCastFunction &f,
                                    TextStream &s);
    static void writeNbBoolExpression(TextStream &s, const BoolCastFunction &f, bool invert = false);

    /**
     *   Writes Python to C++ conversions for arguments on Python wrappers.
     *   If implicit conversions, and thus new object allocation, are needed,
     *   code to deallocate a possible new instance is also generated.
     *   \param s                    text stream to write
     *   \param argType              a pointer to the argument type to be converted
     *   \param argName              C++ argument name
     *   \param pyArgName            Python argument name
     *   \param context              the current meta class
     *   \param defaultValue         an optional default value to be used instead of the conversion result
     *   \param castArgumentAsUnused if true the converted argument is cast as unused to avoid compiler warnings
     */
    qsizetype writeArgumentConversion(TextStream &s, const AbstractMetaType &argType,
                                      const QString &argName, const QString &pyArgName,
                                      ErrorReturn errorReturn,
                                      const AbstractMetaClassCPtr &context = {},
                                      const QString &defaultValue = QString(),
                                      bool castArgumentAsUnused = false) const;

    /**
     *  Returns the AbstractMetaType for a function argument.
     *  If the argument type was modified in the type system, this method will
     *  try to build a new type based on the type name defined in the type system.
     *  \param  func    The function which owns the argument.
     *  \param  index   Argument index in the function signature.
     *  \return The type of the argument indicated by \p index.
     */
    static AbstractMetaType
        getArgumentType(const AbstractMetaFunctionCPtr &func, int index);

    /// Writes the Python to C++ Conversion for function arguments and return
    /// values of virtual methods for wrappers.
    /// \return The number of indirections in case of return types
    qsizetype writePythonToCppTypeConversion(TextStream &s,
                                        const AbstractMetaType &type,
                                        const QString &pyIn,
                                        const QString &cppOut,
                                        const AbstractMetaClassCPtr &context = {},
                                        const QString &defaultValue = {}) const;

    /// Writes the conversion rule for arguments of regular and virtual methods.
    void writeConversionRule(TextStream &s, const AbstractMetaFunctionCPtr &func,
                             TypeSystem::Language language, bool usesPyArgs) const;
    /// Writes the conversion rule for the return value of a method.
    void writeConversionRule(TextStream &s, const AbstractMetaFunctionCPtr &func,
                             TypeSystem::Language language, const QString &outputVar) const;

    /**
     *   Set the Python method wrapper return value variable to Py_None if
     *   there are return types different from void in any of the other overloads
     *   for the function passed as parameter.
     *   \param s text stream to write
     *   \param func a pointer to the function that will possibly return Py_None
     *   \param thereIsReturnValue indicates if the return type of any of the other overloads
     *                             for this function is different from 'void'
     */
    static void writeNoneReturn(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                bool thereIsReturnValue);

    /**
     *   Writes the Python function wrapper overload decisor that selects which C++
     *   method/function to call with the received Python arguments.
     *   \param s text stream to write
     *   \param overloadData the overload data describing all the possible overloads for the function/method
     */
    void writeOverloadedFunctionDecisor(TextStream &s, const OverloadData &overloadData,
                                        const GeneratorContext &classContext,
                                        ErrorReturn errorReturn) const;
    /// Recursive auxiliar method to the other writeOverloadedFunctionDecisor.
    void writeOverloadedFunctionDecisorEngine(TextStream &s,
                                              const OverloadData &overloadData,
                                              const OverloadDataRootNode *node) const;

    static void writeConstructorsNameArgumentResolution(TextStream &s,
                                                        const OverloadData &overloadData,
                                                        NamedArgumentFlags flags,
                                                        const GeneratorContext &context,
                                                        ErrorReturn errorReturn);

    /// Writes calls to all the possible method/function overloads.
    void writeFunctionCalls(TextStream &s,
                            const OverloadData &overloadData,
                            NamedArgumentFlags flags,
                            const GeneratorContext &context,
                            ErrorReturn errorReturn) const;

    /// Writes the call to a single function usually from a collection of overloads.
    void writeSingleFunctionCall(TextStream &s,
                                 const OverloadData &overloadData, NamedArgumentFlags flags,
                                 const AbstractMetaFunctionCPtr &func,
                                 const GeneratorContext &context,
                                 ErrorReturn errorReturn) const;

    /// Returns the name of a C++ to Python conversion function.
    static QString cppToPythonFunctionName(const QString &sourceTypeName, QString targetTypeName = QString());

    /// Returns the name of a Python to C++ conversion function.
    static QString pythonToCppFunctionName(const QString &sourceTypeName, const QString &targetTypeName);
    static QString pythonToCppFunctionName(const AbstractMetaType &sourceType, const AbstractMetaType &targetType);
    static QString pythonToCppFunctionName(const TargetToNativeConversion &toNative,
                                           const TypeEntryCPtr &targetType);

    /// Returns the name of a Python to C++ convertible check function.
    static QString convertibleToCppFunctionName(const QString &sourceTypeName, const QString &targetTypeName);
    static QString convertibleToCppFunctionName(const AbstractMetaType &sourceType, const AbstractMetaType &targetType);
    static QString convertibleToCppFunctionName(const TargetToNativeConversion &toNative,
                                                const TypeEntryCPtr &targetType);

    /// Writes a C++ to Python conversion function.
    void writeCppToPythonFunction(TextStream &s, const QString &code, const QString &sourceTypeName,
                                  const QString &targetTypeName = {}, bool withType = false) const;
    void writeCppToPythonFunction(TextStream &s, const CustomConversionPtr &customConversion) const;
    void writeCppToPythonFunction(TextStream &s, const AbstractMetaType &containerType) const;
    /// Main target type name of a container (for naming the functions).
    static QString containerNativeToTargetTypeName(const ContainerTypeEntryCPtr &type);

    /// Writes a Python to C++ conversion function.
    void writePythonToCppFunction(TextStream &s, const QString &code, const QString &sourceTypeName,
                                  const QString &targetTypeName) const;

    /// Writes a Python to C++ convertible check function.
    static void writeIsPythonConvertibleToCppFunction(TextStream &s,
                                                       const QString &sourceTypeName,
                                                       const QString &targetTypeName,
                                                       const QString &condition,
                                                       QString pythonToCppFuncName = QString(),
                                                       bool acceptNoneAsCppNull = false);

    /// Writes a pair of Python to C++ conversion and check functions.
    void writePythonToCppConversionFunctions(TextStream &s,
                                             const AbstractMetaType &sourceType,
                                             const AbstractMetaType &targetType,
                                             QString typeCheck = QString(),
                                             QString conversion = QString(),
                                             const QString &preConversion = QString()) const;
    /// Writes a pair of Python to C++ conversion and check functions for implicit conversions.
    void writePythonToCppConversionFunctions(TextStream &s,
                                             const TargetToNativeConversion &toNative,
                                             const TypeEntryCPtr &targetType) const;

    /// Writes a pair of Python to C++ conversion and check functions for instantiated
    /// template (smart pointer/container types).
    void writePythonToCppConversionFunctions(TextStream &s,
                                             const AbstractMetaType &templateType) const;

    void writePythonToCppConversionFunction(TextStream &s,
                                            const AbstractMetaType &templateType,
                                            const TargetToNativeConversion &conv) const;

    static void writeAddPythonToCppConversion(TextStream &s, const QString &converterVar,
                                              const QString &pythonToCppFunc,
                                              const QString &isConvertibleFunc);

    static void writeSetPythonToCppPointerConversion(TextStream &s, const QString &converterVar,
                                              const QString &pythonToCppFunc,
                                              const QString &isConvertibleFunc);

    static void writeNamedArgumentResolution(TextStream &s,
                                             const AbstractMetaFunctionCPtr &func,
                                             const OverloadData &overloadData,
                                             NamedArgumentFlags flags,
                                             const GeneratorContext &classContext,
                                             ErrorReturn errorReturn);

    /// Returns a string containing the name of an argument for the given function and argument index.
    static QString argumentNameFromIndex(const ApiExtractorResult &api,
                                         const AbstractMetaFunctionCPtr &func, int argIndex);
    /// Returns the class for an ownership modification of the argument.
    /// Throws if the argument is not a class or cannot be found.
    static AbstractMetaClassCPtr
        argumentClassFromIndex(const ApiExtractorResult &api,
                               const AbstractMetaFunctionCPtr &func, int argIndex);

    void writeMethodCall(TextStream &s, const AbstractMetaFunctionCPtr &func,
                         const GeneratorContext &context, bool usesPyArgs,
                         int maxArgs, const QList<qsizetype> &argumentIndirections,
                         ErrorReturn errorReturn) const;

    static QString getInitFunctionName(const GeneratorContext &context) ;
    static QString getSimpleClassInitFunctionName(const AbstractMetaClassCPtr &metaClass);
    static QString
        getSimpleClassStaticFieldsInitFunctionName(const AbstractMetaClassCPtr &metaClass);

    static void writeSignatureStrings(TextStream &s, const QString &signatures,
                                      const QString &arrayName,
                                      const char *comment);
    void writeInitInheritance(TextStream &s) const;
    void writeClassRegister(TextStream &s,
                            const AbstractMetaClassCPtr &metaClass,
                            const GeneratorContext &classContext,
                            const QString &signatures) const;
    static QStringList pyBaseTypes(const AbstractMetaClassCPtr &metaClass);
    static QString callCppDestructor(const GeneratorContext &classContext,
                                     const QString &dtorClassName);
    static QString destructorFunction(const AbstractMetaClassCPtr &metaClass,
                                      const GeneratorContext &classContext);
    static void writeStaticFieldInitialization(TextStream &s,
                                               const AbstractMetaClassCPtr &metaClass);
    void writeClassDefinition(TextStream &s,
                              const AbstractMetaClassCPtr &metaClass,
                              const GeneratorContext &classContext);
    static void writeClassTypeFunction(TextStream &s,
                                       const GeneratorContext &classContext);
    PyMethodFlags methodDefinitionParameters(const OverloadData &overloadData) const;
    QList<PyMethodDefEntry> methodDefinitionEntries(const OverloadData &overloadData) const;

    void writeSignatureInfo(TextStream &s, const OverloadData &overloadData,
                            bool useProperties=false) const;
    QString signatureParameter(const AbstractMetaArgument &arg, bool implicitConversions) const;
    QString pythonSignature(const AbstractMetaType &type) const;
    /// Writes the implementation of all methods part of python sequence protocol
    void writeSequenceMethods(TextStream &s,
                              const AbstractMetaClassCPtr &metaClass,
                              const GeneratorContext &context) const;
    static void writeTypeAsSequenceDefinition(TextStream &s,
                                         const AbstractMetaClassCPtr &metaClass);

    /// Writes the PyMappingMethods structure for types that supports the python mapping protocol.
    static void writeTypeAsMappingDefinition(TextStream &s,
                                        const AbstractMetaClassCPtr &metaClass);
    void writeMappingMethods(TextStream &s,
                             const AbstractMetaClassCPtr &metaClass,
                             const GeneratorContext &context) const;

    void writeTypeAsNumberDefinition(TextStream &s, const AbstractMetaClassCPtr &metaClass) const;

    static void writeTpTraverseFunction(TextStream &s, const AbstractMetaClassCPtr &metaClass);
    static void writeTpClearFunction(TextStream &s, const AbstractMetaClassCPtr &metaClass);

    static QString writeCopyFunction(TextStream &s, TextStream &definitionStream,
                                     TextStream &signatureStream, const GeneratorContext &context);

    static QString cppFieldAccess(const AbstractMetaField &metaField,
                                  const GeneratorContext &context);
    static void writeGetterFunction(TextStream &s,
                                    const AbstractMetaField &metaField,
                                    const GeneratorContext &context);
    static void writeGetterFunction(TextStream &s,
                                    const QPropertySpec &property,
                                    const GeneratorContext &context);
    static void writeSetterFunctionPreamble(TextStream &s,
                                            const QString &name,
                                            const QString &funcName,
                                            const AbstractMetaType &type,
                                            const GeneratorContext &context);
    static void writeSetterFunction(TextStream &s,
                                    const AbstractMetaField &metaField,
                                    const GeneratorContext &context);
    static void writeSetterFunction(TextStream &s,
                                    const QPropertySpec &property,
                                    const GeneratorContext &context);

    static void writeRichCompareFunctionHeader(TextStream &s,
                                               const QString &baseName,
                                               const GeneratorContext &context);
    void writeRichCompareFunction(TextStream &s, TextStream &t, const GeneratorContext &context) const;
    void writeSmartPointerRichCompareFunction(TextStream &s, const GeneratorContext &context) const;

    static void writeEnumsInitialization(TextStream &s, const char *enclosing,
                                         const AbstractMetaEnumList &enums);
    static void writeEnumsInitFunc(TextStream &s, const QString &funcName,
                                   const AbstractMetaEnumList &enums);
    static bool writeEnumInitialization(TextStream &s, const char *enclosing,
                                        const AbstractMetaEnum &cppEnum);

    static void writeSignalInitialization(TextStream &s, const AbstractMetaClassCPtr &metaClass);

    /// Writes the function that registers the multiple inheritance information
    /// for the classes that need it.
    static void writeMultipleInheritanceInitializerFunction(TextStream &s,
                                                            const AbstractMetaClassCPtr &metaClass);
    /// Writes the implementation of special cast functions, used when we need
    /// to cast a class with multiple inheritance.
    static void writeSpecialCastFunction(TextStream &s, const AbstractMetaClassCPtr &metaClass);

    static void writePrimitiveConverterInitialization(TextStream &s,
                                                      const CustomConversionPtr &customConversion);
    static void writeEnumConverterInitialization(TextStream &s, const AbstractMetaEnum &metaEnum);
    static QString writeContainerConverterInitialization(TextStream &s,
                                                         const AbstractMetaType &type,
                                                         const ApiExtractorResult &api);
    void writeSmartPointerConverterInitialization(TextStream &s, const AbstractMetaType &ype) const;

    static QString typeInitStruct(const TypeEntryCPtr &te);
    static QString typeInitStruct(const GeneratorContext &context);
    static void writeExtendedConverterInitialization(TextStream &s,
                                                     const TypeEntryCPtr &externalType,
                                                     const AbstractMetaClassCList &conversions);

    void writeTypeArrays(TextStream &s);
    QString writeCommonModuleInitFunction(TextStream &s, const QString &convInitFunc,
                                          const QString &containerConvInitFunc,
                                          const QString &qtEnumRegisterMetaTypeFunc);
    void writeModuleExportFunction(TextStream &s, const QString &initFunc,
                                   const QString &execFunc);
    void writeModuleInitFunction(TextStream &s, const QString &initFunc,
                                 const QString &moduleDef);
    void writeModuleExecFunction(TextStream &s, const QString &name,
                                 const QString &lazyTypeCreationFunc,
                                 const QString &opaqueContainerRegisterFunc,
                                 const QString &enumRegisterFunc,
                                 const QString &classPythonDefines,
                                 const AbstractMetaClassCList &classesWithStaticFields);
    static void writeConverterInitFunc(TextStream &s, const QString &funcName,
                                       const QList<CustomConversionPtr> &typeConversions,
                                       const ExtendedConverterData &extendedConverters);
    void writeContainerConverterInitFunc(TextStream &s, const QString &funcName,
                                         const OpaqueContainerTypeHash &opaqueContainers) const;
    static void writeOpaqueContainerConverterRegisterFunc(TextStream &s, const QString &funcName,
                                                          const OpaqueContainerTypeHash &opaqueContainers);
    static void writeQtEnumRegisterMetaTypeFunction(TextStream &s, const QString &name,
                                                    const AbstractMetaEnumList &globalEnums);

    void writeParentChildManagement(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                    bool usesPyArgs,
                                    bool useHeuristicForReturn) const;
    bool writeParentChildManagement(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                    int argIndex,
                                    bool usePyArgs,
                                    bool useHeuristicPolicy) const;
    void writeReturnValueHeuristics(TextStream &s, const AbstractMetaFunctionCPtr &func) const;
    static void writeInitQtMetaTypeFunctionBody(TextStream &s, const GeneratorContext &context);

    /**
     *   Returns the multiple inheritance initializer function for the given class.
     *   \param metaClass the class for whom the function name must be generated.
     *   \return name of the multiple inheritance information initializer function or
     *           an empty string if there is no multiple inheritance in its ancestry.
     */
    static QString multipleInheritanceInitializerFunctionName(const AbstractMetaClassCPtr &metaClass);

    /// Returns a list of all classes to which the given class could be cast.
    static QStringList getAncestorMultipleInheritance(const AbstractMetaClassCPtr &metaClass);

    /// Returns true if the given class supports the python number protocol
    static bool supportsNumberProtocol(const AbstractMetaClassCPtr &metaClass);

    /// Returns true if the given class supports the python sequence protocol
    static bool supportsSequenceProtocol(const AbstractMetaClassCPtr &metaClass) ;

    /// Returns true if the given class supports the python mapping protocol
    static bool supportsMappingProtocol(const AbstractMetaClassCPtr &metaClass) ;

    /// Returns true if generator should produce getters and setters for the given class.
    static bool shouldGenerateGetSetList(const AbstractMetaClassCPtr &metaClass);

    static bool hasHashFunction(const AbstractMetaClassCPtr &c);
    static void writeHashFunction(TextStream &s, TextStream &t, const GeneratorContext &context);

    /// Write default implementations for sequence protocol
    void writeDefaultSequenceMethods(TextStream &s, const GeneratorContext &context) const;
    /// Helper function for writeStdListWrapperMethods.
    static void writeIndexError(TextStream &s, ErrorReturn errorReturn);

    static QString writeReprFunctionHeader(TextStream &s, const GeneratorContext &context);
    static QString writeReprFunction(TextStream &s,
                                     const GeneratorContext &context,
                                     uint indirections);
    static QString writeSmartPointerReprFunction(TextStream &s,
                                                 const GeneratorContext &context);
    static QString writeSmartPointerDirFunction(TextStream &s,
                                                TextStream &definitionStream,
                                                TextStream &signatureStream,
                                                const GeneratorContext &context);
    static void writeReprFunctionFooter(TextStream &s);
    static void writePyMethodDefs(TextStream &s, const QString &className,
                                  const QString &methodsDefinitions);

    void writeModuleCodeSnips(TextStream &s, const CodeSnipList &codeSnips,
                              TypeSystem::CodeSnipPosition position,
                              TypeSystem::Language language) const;

    static bool hasBoolCast(const AbstractMetaClassCPtr &metaClass)
    { return boolCast(metaClass).has_value(); }

    void clearTpFuncs();

    static QString typeInitStructHelper(const TypeEntryCPtr &te, const QString &varName);

    QHash<QString, QString> m_tpFuncs;
    QHash<QString, QString> m_nbFuncs;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(CppGenerator::CppSelfDefinitionFlags)
Q_DECLARE_OPERATORS_FOR_FLAGS(CppGenerator::NamedArgumentFlags)

TextStream &operator<<(TextStream &s, CppGenerator::ErrorReturn r);

#endif // CPPGENERATOR_H
