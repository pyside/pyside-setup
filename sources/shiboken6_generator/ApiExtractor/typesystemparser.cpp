// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "typesystemparser_p.h"
#include "typesystemparser_checks.h"
#include "anystringview_helpers.h"
#include "addedfunction.h"
#include "codesnip.h"
#include "enumtypeentry.h"
#include "containertypeentry.h"
#include "customconversion.h"
#include "customtypenentry.h"
#include "documentation_enums.h"
#include "filecache.h"
#include "flagstypeentry.h"
#include "functiontypeentry.h"
#include "namespacetypeentry.h"
#include "objecttypeentry.h"
#include "primitivetypeentry.h"
#include "smartpointertypeentry.h"
#include "typedefentry.h"
#include "typesystemtypeentry.h"
#include "valuetypeentry.h"
#include "modifications.h"
#include "typedatabase.h"
#include "messages.h"
#include "reporthandler.h"
#include "sourcelocation.h"
#include "conditionalstreamreader.h"


#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qset.h>
#include <QtCore/qstringview.h>
#include <QtCore/qstringalgorithms.h>
#include <QtCore/qversionnumber.h>
#include <QtCore/qxmlstream.h>

#include <algorithm>
#include <array>
#include <optional>
#include <memory>

using namespace Qt::StringLiterals;

constexpr auto allowThreadAttribute = "allow-thread"_L1;
constexpr auto checkFunctionAttribute = "check-function"_L1;
constexpr auto defaultConstructibleAttribute = "default-constructible"_L1;
constexpr auto destructibleAttribute = "destructible"_L1;
constexpr auto copyableAttribute = "copyable"_L1;
constexpr auto smartPointerToPythonConversionAttribute = "to-python"_L1;
constexpr auto movableAttribute = "movable"_L1;
constexpr auto accessAttribute = "access"_L1;
constexpr auto actionAttribute = "action"_L1;
constexpr auto quoteAfterLineAttribute = "quote-after-line"_L1;
constexpr auto quoteBeforeLineAttribute = "quote-before-line"_L1;
constexpr auto textAttribute = "text"_L1;
constexpr auto nameAttribute = "name"_L1;
constexpr auto sinceAttribute = "since"_L1;
constexpr auto untilAttribute = "until"_L1;
constexpr auto defaultSuperclassAttribute = "default-superclass"_L1;
constexpr auto deleteInMainThreadAttribute = "delete-in-main-thread"_L1;
constexpr auto deleteInOwnerThreadAttribute = "delete-in-owner-thread"_L1;
constexpr auto deprecatedAttribute = "deprecated"_L1;
constexpr auto blockingAttribute = "blocking"_L1;
constexpr auto disableWrapperAttribute = "disable-wrapper"_L1;
constexpr auto docFileAttribute = "doc-file"_L1;
constexpr auto exceptionHandlingAttribute = "exception-handling"_L1;
constexpr auto extensibleAttribute = "extensible"_L1;
constexpr auto fileNameAttribute = "file-name"_L1;
constexpr auto fileAttribute = "file"_L1;
constexpr auto flagsAttribute = "flags"_L1;
constexpr auto forceAbstractAttribute = "force-abstract"_L1;
constexpr auto forceIntegerAttribute = "force-integer"_L1;
constexpr auto forceUnsignedAttribute = "force-unsigned"_L1;
constexpr auto formatAttribute = "format"_L1;
constexpr auto generateUsingAttribute = "generate-using"_L1;
constexpr auto generateFunctionsAttribute = "generate-functions"_L1;
constexpr auto classAttribute = "class"_L1;
constexpr auto generateAttribute = "generate"_L1;
constexpr auto generateGetSetDefAttribute = "generate-getsetdef"_L1;
constexpr auto genericClassAttribute = "generic-class"_L1;
constexpr auto indexAttribute = "index"_L1;
constexpr auto invalidateAfterUseAttribute = "invalidate-after-use"_L1;
constexpr auto isNullAttribute = "isNull"_L1;
constexpr auto locationAttribute = "location"_L1;
constexpr auto modifiedTypeAttribute = "modified-type"_L1;
constexpr auto opaqueContainerAttribute = "opaque-containers"_L1;
constexpr auto operatorBoolAttribute = "operator-bool"_L1;
constexpr auto parentManagementAttribute = "parent-management"_L1;
constexpr auto pyiTypeAttribute = "pyi-type"_L1;
constexpr auto overloadNumberAttribute = "overload-number"_L1;
constexpr auto ownershipAttribute = "owner"_L1;
constexpr auto packageAttribute = "package"_L1;
constexpr auto docPackageAttribute = "doc-package"_L1;
constexpr auto docModeAttribute = "doc-mode"_L1;
constexpr auto polymorphicBaseAttribute = "polymorphic-base"_L1;
constexpr auto positionAttribute = "position"_L1;
constexpr auto preferredConversionAttribute = "preferred-conversion"_L1;
constexpr auto preferredTargetLangTypeAttribute = "preferred-target-lang-type"_L1;
constexpr auto pythonEnumTypeAttribute = "python-type"_L1;
constexpr auto pythonOverrideAttribute = "python-override"_L1;
constexpr auto cppEnumTypeAttribute = "cpp-type"_L1;
constexpr auto qtMetaObjectFunctionsAttribute = "qt-metaobject"_L1;
constexpr auto qtMetaTypeAttribute = "qt-register-metatype"_L1;
constexpr auto removeAttribute = "remove"_L1;
constexpr auto renameAttribute = "rename"_L1;
constexpr auto readAttribute = "read"_L1;
constexpr auto targetLangNameAttribute = "target-lang-name"_L1;
constexpr auto writeAttribute = "write"_L1;
constexpr auto opaqueContainerFieldAttribute = "opaque-container"_L1;
constexpr auto replaceAttribute = "replace"_L1;
constexpr auto toAttribute = "to"_L1;
constexpr auto signatureAttribute = "signature"_L1;
constexpr auto snippetAttribute = "snippet"_L1;
constexpr auto snakeCaseAttribute = "snake-case"_L1;
constexpr auto staticAttribute = "static"_L1;
constexpr auto classmethodAttribute = "classmethod"_L1;
constexpr auto threadAttribute = "thread"_L1;
constexpr auto sourceAttribute = "source"_L1;
constexpr auto streamAttribute = "stream"_L1;
constexpr auto privateAttribute = "private"_L1;
constexpr auto usesGilAttribute = "module-uses-gil"_L1;
constexpr auto xPathAttribute = "xpath"_L1;
constexpr auto virtualSlotAttribute = "virtual-slot"_L1;
constexpr auto visibleAttribute = "visible"_L1;
constexpr auto enumIdentifiedByValueAttribute = "identified-by-value"_L1;
constexpr auto subModuleOfAttribute = "submodule-of"_L1;

constexpr auto noAttributeValue = "no"_L1;
constexpr auto yesAttributeValue = "yes"_L1;
constexpr auto trueAttributeValue = "true"_L1;
constexpr auto falseAttributeValue = "false"_L1;

static bool isTypeEntry(StackElement el)
{
    return el >= StackElement::FirstTypeEntry && el <= StackElement::LastTypeEntry;
}

static bool isComplexTypeEntry(StackElement el)
{
    return el >= StackElement::FirstTypeEntry && el <= StackElement::LastComplexTypeEntry;
}

static bool isDocumentation(StackElement el)
{
    return el >= StackElement::FirstDocumentation && el <= StackElement::LastDocumentation;
}

static QList<CustomConversionPtr> customConversionsForReview;

// Set a regular expression for rejection from text. By legacy, those are fixed
// strings, except for '*' meaning 'match all'. Enclosing in "^..$"
// indicates regular expression.
static bool setRejectionRegularExpression(const QString &patternIn,
                                          QRegularExpression *re,
                                          QString *errorMessage)
{
    QString pattern;
    if (patternIn.startsWith(u'^') && patternIn.endsWith(u'$'))
        pattern = patternIn;
    else if (patternIn == u"*")
        pattern = "^.*$"_L1;
    else
        pattern = u'^' + QRegularExpression::escape(patternIn) + u'$';
    re->setPattern(pattern);
    if (!re->isValid()) {
        *errorMessage = msgInvalidRegularExpression(patternIn, re->errorString());
        return false;
    }
    return true;
}

static inline bool hasFileSnippetAttributes(const QXmlStreamAttributes *attributes)
{
    return attributes->hasAttribute(fileAttribute);
}

static QRegularExpression snippetPattern(const QString &snippetLabel)
{
    const QString pattern = R"(^\s*//\s*@snippet\s+)"_L1
                            + QRegularExpression::escape(snippetLabel)
                            + R"(\s*$)"_L1;
    QRegularExpression result(pattern);
    Q_ASSERT(result.isValid());
    return result;
}

template <class EnumType>
struct EnumLookup
{
    QStringView name;
    EnumType value;
};

template <Qt::CaseSensitivity cs, class EnumType, long unsigned N>
static std::optional<EnumType>
    lookupEnum(const std::array<EnumLookup<EnumType>, N> &haystack, QStringView needle)
{
    auto pred = [needle](const EnumLookup<EnumType> &candidate) {
        return candidate.name.compare(needle, cs) == 0;
    };
    auto it = std::find_if(haystack.begin(), haystack.end(), pred);
    if (it != haystack.end())
        return it->value;
    return std::nullopt;
}

static std::optional<TypeSystem::AllowThread> allowThreadFromAttribute(QStringView needle)
{
    using AllowThreadLookup = EnumLookup<TypeSystem::AllowThread>;
    static const std::array haystack{
        AllowThreadLookup{u"yes", TypeSystem::AllowThread::Allow},
        AllowThreadLookup{u"true", TypeSystem::AllowThread::Allow},
        AllowThreadLookup{u"auto", TypeSystem::AllowThread::Auto},
        AllowThreadLookup{u"no", TypeSystem::AllowThread::Disallow},
        AllowThreadLookup{u"false", TypeSystem::AllowThread::Disallow}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<TypeSystem::BoolCast> boolCastFromAttribute(QStringView needle)
{
    using BoolCastLookup = EnumLookup<TypeSystem::BoolCast>;
    static const std::array haystack{
        BoolCastLookup{u"yes", TypeSystem::BoolCast::Enabled},
        BoolCastLookup{u"true", TypeSystem::BoolCast::Enabled},
        BoolCastLookup{u"no", TypeSystem::BoolCast::Disabled},
        BoolCastLookup{u"false", TypeSystem::BoolCast::Disabled},
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<TypeSystem::PythonEnumType> pythonEnumTypeFromAttribute(QStringView needle)
{
    using PythonEnumTypeLookup = EnumLookup<TypeSystem::PythonEnumType>;
    static const std::array haystack{
        PythonEnumTypeLookup{u"Enum", TypeSystem::PythonEnumType::Enum},
        PythonEnumTypeLookup{u"IntEnum", TypeSystem::PythonEnumType::IntEnum},
        PythonEnumTypeLookup{u"Flag", TypeSystem::PythonEnumType::Flag},
        PythonEnumTypeLookup{u"IntFlag", TypeSystem::PythonEnumType::IntFlag},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::DocMode> docModeFromAttribute(QStringView needle)
{
    using DocModeLookup = EnumLookup<TypeSystem::DocMode>;
    static const std::array haystack{
        DocModeLookup{u"nested", TypeSystem::DocMode::Nested},
        DocModeLookup{u"flat", TypeSystem::DocMode::Flat},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::QtMetaTypeRegistration> qtMetaTypeFromAttribute(QStringView needle)
{
    using QtMetaTypeRegistrationLookup = EnumLookup<TypeSystem::QtMetaTypeRegistration>;
    static const std::array haystack{
        QtMetaTypeRegistrationLookup{u"yes", TypeSystem::QtMetaTypeRegistration::Enabled},
        QtMetaTypeRegistrationLookup{u"true", TypeSystem::QtMetaTypeRegistration::Enabled},
        QtMetaTypeRegistrationLookup{u"base", TypeSystem::QtMetaTypeRegistration::BaseEnabled},
        QtMetaTypeRegistrationLookup{u"no", TypeSystem::QtMetaTypeRegistration::Disabled},
        QtMetaTypeRegistrationLookup{u"false", TypeSystem::QtMetaTypeRegistration::Disabled},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::Language> languageFromAttribute(QStringView needle)
{
    using LanguageLookup = EnumLookup<TypeSystem::Language>;
    static const std::array haystack{
        LanguageLookup{u"all", TypeSystem::All}, // sorted!
        LanguageLookup{u"native", TypeSystem::NativeCode}, // em algum lugar do cpp
        LanguageLookup{u"shell", TypeSystem::ShellCode}, // coloca no header, mas antes da declaracao da classe
        LanguageLookup{u"target", TypeSystem::TargetLangCode}  // em algum lugar do cpp
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<DocumentationFormat> documentationFormatFromAttribute(QStringView needle)
{
    using DocumentationFormatLookup = EnumLookup<DocumentationFormat>;
    static const std::array haystack{
        DocumentationFormatLookup{u"native", DocumentationFormat::Native},
        DocumentationFormatLookup{u"target", DocumentationFormat::Target}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<DocumentationEmphasis> documentationEmphasisFromAttribute(QStringView needle)
{
    using DocumentationEmphasisLookup = EnumLookup<DocumentationEmphasis>;
    static const std::array haystack{
        DocumentationEmphasisLookup{u"none", DocumentationEmphasis::None},
        DocumentationEmphasisLookup{u"language-note", DocumentationEmphasis::LanguageNote}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::Ownership> ownershipFromFromAttribute(QStringView needle)
{
    using OwnershipLookup = EnumLookup<TypeSystem::Ownership>;
    static const std::array haystack{
        OwnershipLookup{u"target", TypeSystem::TargetLangOwnership},
        OwnershipLookup{u"c++", TypeSystem::CppOwnership},
        OwnershipLookup{u"default", TypeSystem::DefaultOwnership}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<AddedFunction::Access> addedFunctionAccessFromAttribute(QStringView needle)
{
    using AddedFunctionAccessLookup = EnumLookup<AddedFunction::Access>;
    static const std::array haystack{
        AddedFunctionAccessLookup{u"public", AddedFunction::Public},
        AddedFunctionAccessLookup{u"protected", AddedFunction::Protected},
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<FunctionModification::ModifierFlag> modifierFromAttribute(QStringView needle)
{
    using ModifierFlagLookup = EnumLookup<FunctionModification::ModifierFlag>;
    static const std::array haystack{
        ModifierFlagLookup{u"private", FunctionModification::Private},
        ModifierFlagLookup{u"public", FunctionModification::Public},
        ModifierFlagLookup{u"protected", FunctionModification::Protected},
        ModifierFlagLookup{u"rename", FunctionModification::Rename},
        ModifierFlagLookup{u"final", FunctionModification::Final},
        ModifierFlagLookup{u"non-final", FunctionModification::NonFinal}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<ReferenceCount::Action> referenceCountFromAttribute(QStringView needle)
{
    using ReferenceCountActionLookup = EnumLookup<ReferenceCount::Action>;
    static const std::array haystack{
        ReferenceCountActionLookup{u"add", ReferenceCount::Add},
        ReferenceCountActionLookup{u"add-all", ReferenceCount::AddAll},
        ReferenceCountActionLookup{u"remove", ReferenceCount::Remove},
        ReferenceCountActionLookup{u"set", ReferenceCount::Set},
        ReferenceCountActionLookup{u"ignore", ReferenceCount::Ignore}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<ArgumentOwner::Action> argumentOwnerActionFromAttribute(QStringView needle)
{
    using ArgumentOwnerActionLookup = EnumLookup<ArgumentOwner::Action>;
    static const std::array haystack{
        ArgumentOwnerActionLookup{u"add", ArgumentOwner::Add},
        ArgumentOwnerActionLookup{u"remove", ArgumentOwner::Remove}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<TypeSystem::CodeSnipPosition> codeSnipPositionFromAttribute(QStringView needle)
{
    using CodeSnipPositionLookup = EnumLookup<TypeSystem::CodeSnipPosition>;
    static const std::array haystack{
        CodeSnipPositionLookup{u"beginning", TypeSystem::CodeSnipPositionBeginning},
        CodeSnipPositionLookup{u"end", TypeSystem::CodeSnipPositionEnd},
        CodeSnipPositionLookup{u"declaration", TypeSystem::CodeSnipPositionDeclaration},
        CodeSnipPositionLookup{u"override", TypeSystem::CodeSnipPositionPyOverride},
        CodeSnipPositionLookup{u"wrapper-declaration", TypeSystem::CodeSnipPositionWrapperDeclaration}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<Include::IncludeType> locationFromAttribute(QStringView needle)
{
    using IncludeTypeLookup = EnumLookup<Include::IncludeType>;
    static const std::array haystack{
        IncludeTypeLookup{u"global", Include::IncludePath},
        IncludeTypeLookup{u"local", Include::LocalPath},
        IncludeTypeLookup{u"target", Include::TargetLangImport}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<TypeSystem::DocModificationMode> docModificationFromAttribute(QStringView needle)
{
    using DocModificationModeLookup = EnumLookup<TypeSystem::DocModificationMode>;
    static const std::array haystack{
        DocModificationModeLookup{u"append", TypeSystem::DocModificationAppend},
        DocModificationModeLookup{u"prepend", TypeSystem::DocModificationPrepend},
        DocModificationModeLookup{u"replace", TypeSystem::DocModificationReplace}
    };
    return lookupEnum<Qt::CaseInsensitive>(haystack, needle);
}

static std::optional<DocumentationTarget> docTargetFromAttribute(QStringView needle)
{
    using DocumentationTargetLookup = EnumLookup<DocumentationTarget>;
    static const std::array haystack{
        DocumentationTargetLookup{u"documentation", DocumentationTarget::Documentation},
        DocumentationTargetLookup{u"docstring", DocumentationTarget::DocString}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<ContainerTypeEntry::ContainerKind> containerTypeFromAttribute(QStringView needle)
{
    using ContainerKindLookup = EnumLookup<ContainerTypeEntry::ContainerKind>;
    static const std::array haystack{
        ContainerKindLookup{u"list", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"string-list", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"linked-list", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"vector", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"stack", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"queue", ContainerTypeEntry::ListContainer},
        ContainerKindLookup{u"set", ContainerTypeEntry::SetContainer},
        ContainerKindLookup{u"map", ContainerTypeEntry::MapContainer},
        ContainerKindLookup{u"multi-map", ContainerTypeEntry::MultiMapContainer},
        ContainerKindLookup{u"hash", ContainerTypeEntry::MapContainer},
        ContainerKindLookup{u"multi-hash", ContainerTypeEntry::MultiMapContainer},
        ContainerKindLookup{u"pair", ContainerTypeEntry::PairContainer},
        ContainerKindLookup{u"span", ContainerTypeEntry::SpanContainer}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeRejection::MatchType> typeRejectionFromAttribute(QStringView needle)
{
    using TypeRejectionLookup = EnumLookup<TypeRejection::MatchType>;
    static const std::array haystack{
        TypeRejectionLookup{u"class", TypeRejection::ExcludeClass},
        TypeRejectionLookup{u"function-name", TypeRejection::Function},
        TypeRejectionLookup{u"field-name", TypeRejection::Field},
        TypeRejectionLookup{u"enum-name", TypeRejection::Enum},
        TypeRejectionLookup{u"argument-type", TypeRejection::ArgumentType},
        TypeRejectionLookup{u"return-type", TypeRejection::ReturnType}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::ExceptionHandling> exceptionHandlingFromAttribute(QStringView needle)
{
    using ExceptionHandlingLookup = EnumLookup<TypeSystem::ExceptionHandling>;
    static const std::array haystack{
        ExceptionHandlingLookup{u"no", TypeSystem::ExceptionHandling::Off},
        ExceptionHandlingLookup{u"false", TypeSystem::ExceptionHandling::Off},
        ExceptionHandlingLookup{u"auto-off", TypeSystem::ExceptionHandling::AutoDefaultToOff},
        ExceptionHandlingLookup{u"auto-on", TypeSystem::ExceptionHandling::AutoDefaultToOn},
        ExceptionHandlingLookup{u"yes", TypeSystem::ExceptionHandling::On},
        ExceptionHandlingLookup{u"true", TypeSystem::ExceptionHandling::On},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::SmartPointerType> smartPointerTypeFromAttribute(QStringView needle)
{
    using SmartPointerTypeLookup = EnumLookup<TypeSystem::SmartPointerType>;
    static const std::array haystack{
        SmartPointerTypeLookup{u"handle", TypeSystem::SmartPointerType::Handle},
        SmartPointerTypeLookup{u"unique", TypeSystem::SmartPointerType::Unique},
        SmartPointerTypeLookup{u"value-handle", TypeSystem::SmartPointerType::ValueHandle},
        SmartPointerTypeLookup{u"shared", TypeSystem::SmartPointerType::Shared}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::SmartPointerToPythonConversion>
    smartPointerToPythonConversionFromAttribute(QStringView needle)
{
    using SmartPointerConversionLookup = EnumLookup<TypeSystem::SmartPointerToPythonConversion>;
    static const std::array haystack{
        SmartPointerConversionLookup{u"default", TypeSystem::SmartPointerToPythonConversion::Default},
        SmartPointerConversionLookup{u"null-as-none", TypeSystem::SmartPointerToPythonConversion::NullAsNone}
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

template <class EnumType>
static std::optional<EnumType>
    lookupHashElement(const QHash<QStringView, EnumType> &hash,
                      QStringView needle, Qt::CaseSensitivity cs = Qt::CaseSensitive)
{
    auto end = hash.cend();
    auto it = hash.constFind(needle);
    if (it != end)
        return it.value();
    if (cs == Qt::CaseInsensitive) { // brute force search for the unlikely case mismatch
        for (it = hash.cbegin(); it != end; ++it) {
            if (it.key().compare(needle, cs) == 0)
                return it.value();
        }
    }
    return std::nullopt;
}

using StackElementHash = QHash<QStringView, StackElement>;

static const StackElementHash &stackElementHash()
{
    static const StackElementHash result{
        {u"add-conversion", StackElement::AddConversion},
        {u"add-function", StackElement::AddFunction},
        {u"add-pymethoddef", StackElement::AddPyMethodDef},
        {u"array", StackElement::Array},
        {u"configuration", StackElement::Configuration},
        {u"container-type", StackElement::ContainerTypeEntry},
        {u"conversion-rule", StackElement::ConversionRule},
        {u"custom-constructor", StackElement::Unimplemented},
        {u"custom-destructor", StackElement::Unimplemented},
        {u"custom-type", StackElement::CustomTypeEntry},
        {u"declare-function", StackElement::DeclareFunction},
        {u"define-ownership", StackElement::DefineOwnership},
        {u"enum-type", StackElement::EnumTypeEntry},
        {u"extra-includes", StackElement::ExtraIncludes},
        {u"function", StackElement::FunctionTypeEntry},
        {u"import-file", StackElement::ImportFile},
        {u"include", StackElement::Include},
        {u"inject-code", StackElement::InjectCode},
        {u"inject-documentation", StackElement::InjectDocumentation},
        {u"insert-template", StackElement::InsertTemplate},
        {u"interface-type", StackElement::InterfaceTypeEntry},
        {u"load-typesystem", StackElement::LoadTypesystem},
        {u"modify-argument", StackElement::ModifyArgument},
        {u"modify-documentation", StackElement::ModifyDocumentation},
        {u"modify-field", StackElement::ModifyField},
        {u"modify-function", StackElement::ModifyFunction},
        {u"namespace-type", StackElement::NamespaceTypeEntry},
        {u"native-to-target", StackElement::NativeToTarget},
        {u"no-null-pointer", StackElement::NoNullPointers},
        {u"object-type", StackElement::ObjectTypeEntry},
        {u"opaque-container", StackElement::OpaqueContainer},
        {u"overload-removal", StackElement::OverloadRemoval},
        {u"parent", StackElement::ParentOwner},
        {u"primitive-type", StackElement::PrimitiveTypeEntry},
        {u"property", StackElement::Property},
        {u"reference-count", StackElement::ReferenceCount},
        {u"reject-enum-value", StackElement::RejectEnumValue},
        {u"rejection", StackElement::Rejection},
        {u"remove-argument", StackElement::RemoveArgument},
        {u"remove-default-expression", StackElement::RemoveDefaultExpression},
        {u"rename", StackElement::Rename}, // ### fixme PySide7: remove
        {u"replace", StackElement::Replace},
        {u"replace-default-expression", StackElement::ReplaceDefaultExpression},
        {u"replace-type", StackElement::ReplaceType},
        {u"smart-pointer-type", StackElement::SmartPointerTypeEntry},
        {u"suppress-warning", StackElement::SuppressedWarning},
        {u"system-include", StackElement::SystemInclude},
        {u"target-to-native", StackElement::TargetToNative},
        {u"template", StackElement::Template},
        {u"typedef-type", StackElement::TypedefTypeEntry},
        {u"typesystem", StackElement::Root},
        {u"value-type", StackElement::ValueTypeEntry},
    };
    return result;
}

static std::optional<StackElement> elementFromTag(QStringView needle)
{
     return lookupHashElement(stackElementHash(), needle,
                              Qt::CaseInsensitive); // FIXME PYSIDE-7: case sensitive
}

static QStringView tagFromElement(StackElement st)
{
    return stackElementHash().key(st);
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, StackElement st)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << tagFromElement(st);
    return d;
}
#endif // QT_NO_DEBUG_STREAM

static std::optional<TypeSystem::SnakeCase> snakeCaseFromAttribute(QStringView needle)
{
    using SnakeCaseLookup = EnumLookup<TypeSystem::SnakeCase>;
    static const std::array haystack{
        SnakeCaseLookup{u"no", TypeSystem::SnakeCase::Disabled},
        SnakeCaseLookup{u"false", TypeSystem::SnakeCase::Disabled},
        SnakeCaseLookup{u"yes", TypeSystem::SnakeCase::Enabled},
        SnakeCaseLookup{u"true", TypeSystem::SnakeCase::Enabled},
        SnakeCaseLookup{u"both", TypeSystem::SnakeCase::Both},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static std::optional<TypeSystem::Visibility> visibilityFromAttribute(QStringView needle)
{
    using VisibilityLookup = EnumLookup<TypeSystem::Visibility>;
    static const std::array haystack{
        VisibilityLookup{u"no", TypeSystem::Visibility::Invisible},
        VisibilityLookup{u"false", TypeSystem::Visibility::Invisible},
        VisibilityLookup{u"auto", TypeSystem::Visibility::Auto},
        VisibilityLookup{u"yes", TypeSystem::Visibility::Visible},
        VisibilityLookup{u"true", TypeSystem::Visibility::Visible},
    };
    return lookupEnum<Qt::CaseSensitive>(haystack, needle);
}

static qsizetype indexOfAttribute(const QXmlStreamAttributes &atts,
                                  QAnyStringView name)
{
    for (qsizetype i = 0, size = atts.size(); i < size; ++i) {
        if (atts.at(i).qualifiedName() == name)
            return i;
    }
    return -1;
}

static QString msgMissingAttribute(const QString &a)
{
    return u"Required attribute '"_s + a
        + u"' missing."_s;
}

QTextStream &operator<<(QTextStream &str, const QXmlStreamAttribute &attribute)
{
    str << attribute.qualifiedName() << "=\"" << attribute.value() << '"';
    return str;
}

static QString msgInvalidAttributeValue(const QXmlStreamAttribute &attribute)
{
    QString result;
    QTextStream(&result) << "Invalid attribute value:" << attribute;
    return result;
}

static QString msgUnusedAttributes(QStringView tag, const QXmlStreamAttributes &attributes)
{
    QString result;
    QTextStream str(&result);
    str << attributes.size() << " attributes(s) unused on <" << tag << ">: ";
    for (qsizetype i = 0, size = attributes.size(); i < size; ++i) {
        if (i)
            str << ", ";
        str << attributes.at(i);
    }
    return result;
}

// QXmlStreamEntityResolver::resolveEntity(publicId, systemId) is not
// implemented; resolve via undeclared entities instead.
class TypeSystemEntityResolver : public QXmlStreamEntityResolver
{
public:
    explicit TypeSystemEntityResolver(const QString &currentPath) :
        m_currentPath(currentPath) {}

    QString resolveUndeclaredEntity(const QString &name) override;

private:
    QString readFile(const QString &entityName, QString *errorMessage) const;

    const QString m_currentPath;
};

QString TypeSystemEntityResolver::readFile(const QString &entityName, QString *errorMessage) const
{
    QString fileName = entityName;
    if (!fileName.contains(u'.'))
        fileName += u".xml"_s;
    QString path = TypeDatabase::instance()->modifiedTypesystemFilepath(fileName, m_currentPath);
    if (!QFileInfo::exists(path)) // PySide6-specific hack
        fileName.prepend(u"typesystem_"_s);
    path = TypeDatabase::instance()->modifiedTypesystemFilepath(fileName, m_currentPath);
    if (!QFileInfo::exists(path)) {
        *errorMessage = u"Unable to resolve: "_s + entityName;
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = msgCannotOpenForReading(file);
        return {};
    }
    QString result = QString::fromUtf8(file.readAll()).trimmed();
    // Remove license header comments on which QXmlStreamReader chokes
    if (result.startsWith(u"<!--")) {
        const auto commentEnd = result.indexOf(u"-->");
        if (commentEnd != -1) {
            result.remove(0, commentEnd + 3);
            result = result.trimmed();
        }
    }
    return result;
}

QString TypeSystemEntityResolver::resolveUndeclaredEntity(const QString &name)
{
    QString errorMessage;
    const QString result = readFile(name, &errorMessage);
    if (result.isEmpty()) { // The parser will fail and display the line number.
        qCWarning(lcShiboken, "%s",
                  qPrintable(msgCannotResolveEntity(name, errorMessage)));
    }
    return result;
}

// State depending on element stack
enum class ParserState : std::uint8_t
{
    None,
    PrimitiveTypeNativeToTargetConversion,
    PrimitiveTypeTargetToNativeConversion,
    ArgumentConversion, // Argument conversion rule with class attribute
    ArgumentNativeToTargetConversion,
    ArgumentTargetToNativeConversion,
    FunctionCodeInjection,
    TypeEntryCodeInjection,
    TypeSystemCodeInjection,
    Template
};

TypeSystemParser::TypeSystemParser(const std::shared_ptr<TypeDatabaseParserContext> &context,
                                   bool generate) :
    m_context(context),
    m_generate(generate ? TypeEntry::GenerateCode : TypeEntry::GenerateForSubclass)
{
}

TypeSystemParser::~TypeSystemParser() = default;

static QString readerFileName(const ConditionalStreamReader &reader)
{
    const auto *file = qobject_cast<const QFile *>(reader.device());
    return file != nullptr ? file->fileName() : QString();
}

static QString msgReaderMessage(const ConditionalStreamReader &reader,
                                const char *type,
                                const QString &what)
{
    QString message;
    QTextStream str(&message);
    const QString fileName = readerFileName(reader);
    if (fileName.isEmpty())
        str << "<stdin>:";
    else
        str << QDir::toNativeSeparators(fileName) << ':';
    // Use a tab separator like SourceLocation for suppression detection
    str << reader.lineNumber() << ':' << reader.columnNumber()
        << ":\t" << type << ": " << what;
    return message;
}

static QString msgReaderWarning(const ConditionalStreamReader &reader, const QString &what)
{
    return  msgReaderMessage(reader, "Warning", what);
}

static QString msgReaderError(const ConditionalStreamReader &reader, const QString &what)
{
    return  msgReaderMessage(reader, "Error", what);
}

static QString msgUnimplementedElementWarning(const ConditionalStreamReader &reader,
                                              QAnyStringView name)
{
    QString message;
    QTextStream(&message) << "The element \"" << name
        << "\" is not implemented.";
    return msgReaderMessage(reader, "Warning", message);
}

static QString msgUnimplementedAttributeWarning(const ConditionalStreamReader &reader,
                                                QStringView name)
{
    QString message;
    QTextStream(&message) <<  "The attribute \"" << name
        << "\" is not implemented.";
    return msgReaderMessage(reader, "Warning", message);
}

static inline QString msgUnimplementedAttributeWarning(const ConditionalStreamReader &reader,
                                                       const QXmlStreamAttribute &attribute)
{
    return msgUnimplementedAttributeWarning(reader, attribute.qualifiedName());
}

static QString
    msgUnimplementedAttributeValueWarning(const ConditionalStreamReader &reader,
                                          QAnyStringView name, QAnyStringView value)
{
    QString message;
    QTextStream(&message) << "The value \"" << value
        << "\" of the attribute \"" << name << "\" is not implemented.";
    return msgReaderMessage(reader, "Warning", message);
}

static inline
    QString msgUnimplementedAttributeValueWarning(const ConditionalStreamReader &reader,
                                                  const QXmlStreamAttribute &attribute)
{
    return msgUnimplementedAttributeValueWarning(reader,
                                                 attribute.qualifiedName(),
                                                 attribute.value());
}

static bool addRejection(TypeDatabase *database, bool generate, QXmlStreamAttributes *attributes,
                         QString *errorMessage)
{
    const auto classIndex = indexOfAttribute(*attributes, classAttribute);
    if (classIndex == -1) {
        *errorMessage = msgMissingAttribute(classAttribute);
        return false;
    }

    TypeRejection rejection;
    rejection.generate = generate;
    const QString className = attributes->takeAt(classIndex).value().toString();
    if (!setRejectionRegularExpression(className, &rejection.className, errorMessage))
        return false;

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto &attribute = attributes->at(i);
        const auto name = attribute.qualifiedName();
        const auto typeOpt = typeRejectionFromAttribute(name);
        if (!typeOpt.has_value()) {
            *errorMessage = msgInvalidAttributeValue(attribute);
            return false;
        }
        switch (typeOpt.value()) {
        case TypeRejection::Function:
        case TypeRejection::Field:
        case TypeRejection::Enum:
        case TypeRejection::ArgumentType:
        case TypeRejection::ReturnType: {
            const QString pattern = attributes->takeAt(i).value().toString();
            if (!setRejectionRegularExpression(pattern, &rejection.pattern, errorMessage))
                return false;
            rejection.matchType = typeOpt.value();
            database->addRejection(rejection);
            return true;
        }
        case TypeRejection::ExcludeClass:
            break;
        }
    }

    // Special case: When all fields except class are empty, completely exclude class
    if (className == u"*") {
        *errorMessage = u"bad reject entry, neither 'class', 'function-name'"
                         " nor 'field' specified"_s;
        return false;
    }
    rejection.matchType = TypeRejection::ExcludeClass;
    database->addRejection(rejection);
    return true;
}

bool TypeSystemParser::parse(ConditionalStreamReader &reader)
{
    m_error.clear();
    m_currentPath.clear();
    m_currentFile.clear();
    return parseXml(reader);
}

bool TypeSystemParser::parseXml(ConditionalStreamReader &reader)
{
    const QString fileName = readerFileName(reader);
    if (!fileName.isEmpty()) {
        QFileInfo fi(fileName);
        m_currentPath = fi.absolutePath();
        m_currentFile = fi.absoluteFilePath();
    }
    m_entityResolver = std::make_shared<TypeSystemEntityResolver>(m_currentPath);
    reader.setEntityResolver(m_entityResolver);

    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::NoToken:
        case QXmlStreamReader::Invalid:
            m_error = msgReaderError(reader, reader.errorString());
            return false;
        case QXmlStreamReader::StartElement: {
            const auto elementTypeOpt = elementFromTag(reader.name());
            if (!elementTypeOpt.has_value()) {
                m_error = u"Unknown tag name: '"_s + reader.name().toString() + u'\'';
                return false;
            }
            m_stack.push(elementTypeOpt.value());
            if (!startElement(reader, m_stack.top())) {
                m_error = msgReaderError(reader, m_error);
                return false;
            }
        }
            break;
        case QXmlStreamReader::EndElement:
            if (!endElement(m_stack.top())) {
                m_error = msgReaderError(reader, m_error);
                return false;
            }
            m_stack.pop();
            break;
        case QXmlStreamReader::Characters:
            if (!characters(reader.text())) {
                m_error = msgReaderError(reader, m_error);
                return false;
            }
            break;
        case QXmlStreamReader::StartDocument:
        case QXmlStreamReader::EndDocument:
        case QXmlStreamReader::Comment:
        case QXmlStreamReader::DTD:
        case QXmlStreamReader::EntityReference:
        case QXmlStreamReader::ProcessingInstruction:
            break;
        }
    }
    return true;
}

bool TypeSystemParser::endElement(StackElement element)
{
    if (m_ignoreDepth) {
        --m_ignoreDepth;
        return true;
    }

    if (m_currentDroppedEntryDepth != 0) {
        --m_currentDroppedEntryDepth;
        return true;
    }

    if (element == StackElement::ImportFile)
        return true;

    if (m_contextStack.isEmpty())
        return true;

    const auto &top = m_contextStack.top();

    switch (element) {
    case StackElement::Unimplemented:
        return true;
    case StackElement::Root:
        if (m_generate == TypeEntry::GenerateCode) {
            TypeDatabase::instance()->addGlobalUserFunctions(top->addedFunctions);
            TypeDatabase::instance()->addGlobalUserFunctionModifications(top->functionMods);
            for (const auto &customConversion : std::as_const(customConversionsForReview)) {
                TargetToNativeConversions &toNatives =
                    customConversion->targetToNativeConversions();
                for (auto &toNative : toNatives)
                    toNative.setSourceType(m_context->db->findType(toNative.sourceTypeName()));
            }
        }
        purgeEmptyCodeSnips(&std::static_pointer_cast<TypeSystemTypeEntry>(top->entry)->codeSnips());
        break;
    case StackElement::FunctionTypeEntry:
        TypeDatabase::instance()->addGlobalUserFunctionModifications(top->functionMods);
        break;
    case StackElement::ObjectTypeEntry:
    case StackElement::ValueTypeEntry:
    case StackElement::InterfaceTypeEntry:
    case StackElement::ContainerTypeEntry:
    case StackElement::NamespaceTypeEntry: {
        Q_ASSERT(top->entry);
        Q_ASSERT(top->entry->isComplex());
        auto centry = std::static_pointer_cast<ComplexTypeEntry>(top->entry);
        purgeEmptyCodeSnips(&centry->codeSnips());
        centry->setAddedFunctions(top->addedFunctions);
        centry->setFunctionModifications(top->functionMods);
        centry->setFieldModifications(top->fieldMods);
        centry->setDocModification(top->docModifications);
    }
    break;

    case StackElement::TypedefTypeEntry: {
        auto centry = std::static_pointer_cast<TypedefEntry>(top->entry)->target();
        centry->setAddedFunctions(centry->addedFunctions() + top->addedFunctions);
        centry->setFunctionModifications(centry->functionModifications() + top->functionMods);
        centry->setFieldModifications(centry->fieldModifications() + top->fieldMods);
        centry->setDocModification(centry->docModifications() + top->docModifications);
        if (top->entry->isComplex()) {
            auto cte = std::static_pointer_cast<const ComplexTypeEntry>(top->entry);
            centry->setCodeSnips(centry->codeSnips() + cte->codeSnips());
        }
    }
    break;

    case StackElement::AddFunction:
    case StackElement::DeclareFunction: {
        // Leaving add-function: Assign all modifications to the added function
        const int modIndex = top->addedFunctionModificationIndex;
        top->addedFunctionModificationIndex = -1;
        Q_ASSERT(modIndex >= 0);
        Q_ASSERT(!top->addedFunctions.isEmpty());
        while (modIndex < top->functionMods.size())
            top->addedFunctions.last()->modifications().append(top->functionMods.takeAt(modIndex));
    }
    break;
    case StackElement::NativeToTarget:
    case StackElement::AddConversion:
        switch (parserState()) {
        case ParserState::PrimitiveTypeNativeToTargetConversion:
        case ParserState::PrimitiveTypeTargetToNativeConversion: {
            auto customConversion = CustomConversion::getCustomConversion(top->entry);
            if (!customConversion) {
                m_error = msgMissingCustomConversion(top->entry);
                return false;
            }
            QString code = top->conversionCodeSnips.constLast().code();
            if (element == StackElement::AddConversion) {
                if (customConversion->targetToNativeConversions().isEmpty()) {
                    m_error = u"CustomConversion's target to native conversions missing."_s;
                    return false;
                }
                customConversion->targetToNativeConversions().last().setConversion(code);
            } else {
                customConversion->setNativeToTargetConversion(code);
            }
        }
            break;

        case ParserState::ArgumentNativeToTargetConversion: {
            top->conversionCodeSnips.last().language = TypeSystem::TargetLangCode;
            auto &lastArgMod = m_contextStack.top()->functionMods.last().argument_mods().last();
            lastArgMod.conversionRules().append(top->conversionCodeSnips.constLast());
        }
            break;
        case ParserState::ArgumentTargetToNativeConversion: {
            top->conversionCodeSnips.last().language = TypeSystem::NativeCode;
            auto &lastArgMod = m_contextStack.top()->functionMods.last().argument_mods().last();
            lastArgMod.conversionRules().append(top->conversionCodeSnips.constLast());
        }
            break;
        default:
            break;
        }
        top->conversionCodeSnips.clear();
        break;

    case StackElement::EnumTypeEntry:
        m_currentEnum = nullptr;
        break;
    case StackElement::Template:
        m_context->db->addTemplate(m_templateEntry);
        m_templateEntry = nullptr;
        break;
    case StackElement::InsertTemplate:
        if (auto *snip = injectCodeTarget(1)) {
            Q_ASSERT(m_templateInstance.has_value());
            snip->addTemplateInstance(m_templateInstance.value());
        }
        m_templateInstance.reset();
        break;

    case StackElement::ModifyArgument:
        purgeEmptyCodeSnips(&top->functionMods.last().argument_mods().last().conversionRules());
        break;

    default:
        break;
    }

    if (isTypeEntry(element) || element == StackElement::Root)
        m_contextStack.pop();

    return true;
}

ParserState TypeSystemParser::parserState(qsizetype offset) const
{
    const auto stackSize = m_stack.size() - offset;
    if (stackSize <= 0 || m_contextStack.isEmpty())
        return ParserState::None;

    const auto last = stackSize - 1;

    switch (m_stack.at(last)) {
        // Primitive entry with conversion rule
    case StackElement::NativeToTarget: // <conversion-rule><native-to-target>
        if (stackSize > 2 && m_stack.at(last - 2) == StackElement::ModifyArgument)
            return ParserState::ArgumentNativeToTargetConversion;
        return ParserState::PrimitiveTypeNativeToTargetConversion;

    case StackElement::AddConversion: // <conversion-rule><target-to-native><add-conversion>
        if (stackSize > 3 && m_stack.at(last - 3) == StackElement::ModifyArgument)
            return ParserState::ArgumentTargetToNativeConversion;
        return ParserState::PrimitiveTypeTargetToNativeConversion;

    case StackElement::ConversionRule:
        if (stackSize > 1 && m_stack.at(last - 1) == StackElement::ModifyArgument)
            return ParserState::ArgumentConversion;
        break;

    case StackElement::InjectCode:
        switch (m_stack.value(last - 1, StackElement::None)) {
        case StackElement::Root:
            return ParserState::TypeSystemCodeInjection;
        case StackElement::ModifyFunction:
        case StackElement::AddFunction:
            return ParserState::FunctionCodeInjection;
        case StackElement::NamespaceTypeEntry:
        case StackElement::ObjectTypeEntry:
        case StackElement::ValueTypeEntry:
        case StackElement::InterfaceTypeEntry:
            return ParserState::TypeEntryCodeInjection;
        default:
            break;
        }
        break;

    case StackElement::Template:
        return ParserState::Template;

    default:
        break;
    }

    return ParserState::None;
}

// Return where to add injected code depending on elements.
CodeSnipAbstract *TypeSystemParser::injectCodeTarget(qsizetype offset) const
{
    const auto state = parserState(offset);
    if (state == ParserState::None)
        return nullptr;

    const auto &top = m_contextStack.top();
    switch (state) {
    case ParserState::PrimitiveTypeNativeToTargetConversion:
    case ParserState::PrimitiveTypeTargetToNativeConversion:
    case ParserState::ArgumentNativeToTargetConversion:
    case ParserState::ArgumentTargetToNativeConversion:
        return &top->conversionCodeSnips.last();
    case ParserState::ArgumentConversion:
        return &top->functionMods.last().argument_mods().last().conversionRules().last();
    case ParserState::FunctionCodeInjection: {
        auto &funcMod = top->functionMods.last();
        funcMod.setModifierFlag(FunctionModification::CodeInjection);
        return &funcMod.snips().last();
    }
    case ParserState::TypeEntryCodeInjection:
        Q_ASSERT(top->entry->isComplex());
        return &std::static_pointer_cast<ComplexTypeEntry>(top->entry)->codeSnips().last();
    case ParserState::TypeSystemCodeInjection:
        Q_ASSERT(top->entry->isTypeSystem());
        return &std::static_pointer_cast<TypeSystemTypeEntry>(top->entry)->codeSnips().last();
    case ParserState::Template:
        return m_templateEntry.get();
    default:
        break;
    }

    return nullptr;
}

template <class String> // QString/QStringRef
bool TypeSystemParser::characters(const String &ch)
{
    const auto stackSize = m_stack.size();
    if (m_currentDroppedEntryDepth != 0 || m_ignoreDepth != 0
        || stackSize == 0 || m_stack.top() == StackElement::Unimplemented) {
        return true;
    }

    const StackElement type =  m_stack.top();

    if (type == StackElement::Template) {
        m_templateEntry->addCode(ch);
        return true;
    }

    if (m_contextStack.isEmpty()) {
        m_error = msgNoRootTypeSystemEntry();
        return false;
    }

    if (auto *snip = injectCodeTarget()) {
        snip->addCode(ch);
        return true;
    }

    if (isDocumentation(type)) {
        const bool isAddedFunction = m_stack.value(m_stack.size() - 2, StackElement::None)
                                     == StackElement::AddFunction;
        const auto &top = m_contextStack.top();
        auto &docModifications = isAddedFunction
            ? top->addedFunctions.last()->docModifications()
            : top->docModifications;
        docModifications.last().setCode(ch);
    }

    return true;
}

bool TypeSystemParser::importFileElement(const QXmlStreamAttributes &atts)
{
    const QString fileName = atts.value(nameAttribute).toString();
    if (fileName.isEmpty()) {
        m_error = u"Required attribute 'name' missing for include-file tag."_s;
        return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        file.setFileName(u":/trolltech/generator/"_s + fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_error = msgCannotOpenForReading(file);
            return false;
        }
    }

    const auto quoteFrom = atts.value(quoteAfterLineAttribute);
    bool foundFromOk = quoteFrom.isEmpty();
    bool from = quoteFrom.isEmpty();

    const auto quoteTo = atts.value(quoteBeforeLineAttribute);
    bool foundToOk = quoteTo.isEmpty();
    bool to = true;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (from && to && line.contains(quoteTo)) {
            to = false;
            foundToOk = true;
            break;
        }
        if (from && to)
            characters(line + u'\n');
        if (!from && line.contains(quoteFrom)) {
            from = true;
            foundFromOk = true;
        }
    }
    if (!foundFromOk || !foundToOk) {
        QString fromError = "Could not find quote-after-line='%1' in file '%2'."_L1
                            .arg(quoteFrom.toString(), fileName);
        QString toError = "Could not find quote-before-line='%1' in file '%2'."_L1
                          .arg(quoteTo.toString(), fileName);

        if (!foundToOk)
            m_error = toError;
        if (!foundFromOk)
            m_error = fromError;
        if (!foundFromOk && !foundToOk)
            m_error = fromError + u' ' + toError;
        return false;
    }

    return true;
}

static bool convertBoolean(QStringView value, QAnyStringView attributeName, bool defaultValue)
{
    if (value.compare(trueAttributeValue, Qt::CaseInsensitive) == 0
        || value.compare(yesAttributeValue, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (value.compare(falseAttributeValue, Qt::CaseInsensitive) == 0
        || value.compare(noAttributeValue, Qt::CaseInsensitive) == 0) {
        return false;
    }
    qCWarning(lcShiboken).noquote().nospace() << "Boolean value '" << value
        << "' not supported in attribute '" << attributeName
        << "'. Use 'yes' or 'no'. Defaulting to '"
        << (defaultValue ? yesAttributeValue : noAttributeValue) << "'.";
    return defaultValue;
}

static bool convertRemovalAttribute(QStringView value)
{
    return value == u"all" // Legacy
        || convertBoolean(value, removeAttribute, false);
}

// Check whether an entry should be dropped, allowing for dropping the module
// name (match 'Class' and 'Module.Class').
static bool shouldDropTypeEntry(const TypeDatabase *db,
                                const TypeSystemParser::ContextStack &stack                                ,
                                QString name)
{
    for (auto i = stack.size() - 1; i >= 0; --i) {
        if (auto entry = stack.at(i)->entry) {
            if (entry->type() == TypeEntry::TypeSystemType) {
                if (db->shouldDropTypeEntry(name)) // Unqualified
                    return true;
            }
            name.prepend(u'.');
            name.prepend(entry->name());
        }
    }
    return db->shouldDropTypeEntry(name);
}

// Returns empty string if there's no error.
static QString checkSignatureError(const QString& signature, const QString& tag)
{
    QString funcName = signature.left(signature.indexOf(u'(')).trimmed();
    static const QRegularExpression whiteSpace("\\s"_L1);
    Q_ASSERT(whiteSpace.isValid());
    if (!funcName.startsWith(u"operator ") && funcName.contains(whiteSpace)) {
        return QString::fromLatin1("Error in <%1> tag signature attribute '%2'.\n"
                                   "White spaces aren't allowed in function names, "
                                   "and return types should not be part of the signature.")
                                   .arg(tag, signature);
    }
    return {};
}

inline TypeEntryCPtr TypeSystemParser::currentParentTypeEntry() const
{
    const auto size = m_contextStack.size();
    return size > 1 ? m_contextStack.at(size - 2)->entry : nullptr;
}

bool TypeSystemParser::checkRootElement()
{
    for (auto i = m_contextStack.size() - 1; i >= 0; --i) {
        auto e = m_contextStack.at(i)->entry;
        if (e && e->isTypeSystem())
            return true;
    }
    m_error = msgNoRootTypeSystemEntry();
    return false;
}

static CppTypeEntryCPtr findViewedType(const QString &name)
{
    const auto range = TypeDatabase::instance()->entries().equal_range(name);
    for (auto i = range.first; i != range.second; ++i) {
        switch (i.value()->type()) {
        case TypeEntry::BasicValueType:
        case TypeEntry::PrimitiveType:
        case TypeEntry::ContainerType:
        case TypeEntry::ObjectType:
            return std::dynamic_pointer_cast<const CppTypeEntry>(i.value());
        default:
            break;
        }
    }
    return nullptr;
}

bool TypeSystemParser::applyCommonAttributes(const ConditionalStreamReader &reader,
                                             const TypeEntryPtr &type,
                                             QXmlStreamAttributes *attributes)
{
    type->setSourceLocation(SourceLocation(m_currentFile,
                                           reader.lineNumber()));
    type->setCodeGeneration(m_generate);
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name ==  u"revision") {
            type->setRevision(attributes->takeAt(i).value().toInt());
        }
    }
    return true;
}

bool TypeSystemParser::applyCppAttributes(const ConditionalStreamReader &reader,
                                          const CppTypeEntryPtr &type,
                                          QXmlStreamAttributes *attributes)
{
    if (!applyCommonAttributes(reader, type, attributes))
        return false;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"default-constructor") {
            type->setDefaultConstructor(attributes->takeAt(i).value().toString());
        } else if (name == u"view-on") {
            const QString name = attributes->takeAt(i).value().toString();
            auto views = findViewedType(name);
            if (!views) {
                m_error = msgCannotFindView(name, type->name());
                return false;
            }
            type->setViewOn(views);
        } else if (name == defaultConstructibleAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          defaultConstructibleAttribute, false);
            type->setDefaultConstructibleFlag(v ? TypeSystem::DefaultConstructibleFlag::Enabled
                                                : TypeSystem::DefaultConstructibleFlag::Disabled);
        } else if (name == copyableAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          copyableAttribute, false);
            type->setCopyableFlag(v ? TypeSystem::CopyableFlag::Enabled
                                    : TypeSystem::CopyableFlag::Disabled);
        } else if (name == movableAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          movableAttribute, false);
            type->setMovableFlag(v ? TypeSystem::MovableFlag::Enabled
                                   : TypeSystem::MovableFlag::Disabled);
        } else if (name == destructibleAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          destructibleAttribute, false);
            type->setDestructibleFlag(v ? TypeSystem::DestructibleFlag::Enabled
                                      : TypeSystem::DestructibleFlag::Disabled);
        } else if (name == qtMetaTypeAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto qtMetaTypeOpt = qtMetaTypeFromAttribute(attribute.value());
            if (qtMetaTypeOpt.has_value()) {
                type->setQtMetaTypeRegistration(qtMetaTypeOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        }
    }
    return true;
}

CustomTypeEntryPtr TypeSystemParser::parseCustomTypeEntry(const ConditionalStreamReader &,
                                                        const QString &name,
                                                        const QVersionNumber &since,
                                                        QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    auto result = std::make_shared<CustomTypeEntry>(name, since, m_contextStack.top()->entry);
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == checkFunctionAttribute)
            result->setCheckFunction(attributes->takeAt(i).value().toString());
    }
    return result;
}

FlagsTypeEntryPtr
    TypeSystemParser::parseFlagsEntry(const ConditionalStreamReader &reader,
                             const EnumTypeEntryPtr &enumEntry, QString flagName,
                             const QVersionNumber &since,
                             QXmlStreamAttributes *attributes)

{
    if (!checkRootElement())
        return nullptr;
    auto ftype = std::make_shared<FlagsTypeEntry>(u"QFlags<"_s + enumEntry->name() + u'>',
                                                  since,
                                                  typeSystemTypeEntry(currentParentTypeEntry()));
    ftype->setOriginator(enumEntry);
    ftype->setTargetLangPackage(enumEntry->targetLangPackage());
    // Try toenumEntry get the guess the qualified flag name
    if (!flagName.contains(u"::"_s)) {
        auto eq = enumEntry->qualifier();
        if (!eq.isEmpty())
            flagName.prepend(eq + u"::"_s);
    }

    ftype->setOriginalName(flagName);
    if (!applyCppAttributes(reader, ftype, attributes))
        return nullptr;

    QStringList lst = flagName.split(u"::"_s);
    const QString name = lst.takeLast();
    const QString targetLangFlagName = lst.join(u'.');
    const QString &targetLangQualifier = enumEntry->targetLangQualifier();
    if (targetLangFlagName != targetLangQualifier) {
        qCWarning(lcShiboken, "enum %s and flags %s (%s) differ in qualifiers",
                  qPrintable(targetLangQualifier), qPrintable(lst.value(0)),
                  qPrintable(targetLangFlagName));
    }

    ftype->setFlagsName(name);
    enumEntry->setFlags(ftype);

    m_context->db->addFlagsType(ftype);
    m_context->db->addType(ftype);

    const auto revisionIndex = indexOfAttribute(*attributes, u"flags-revision");
    ftype->setRevision(revisionIndex != -1
                       ? attributes->takeAt(revisionIndex).value().toInt()
                       : enumEntry->revision());
    return ftype;
}

SmartPointerTypeEntryPtr
    TypeSystemParser::parseSmartPointerEntry(const ConditionalStreamReader &reader,
                                    const QString &name, const QVersionNumber &since,
                                    QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    TypeSystem::SmartPointerType smartPointerType = TypeSystem::SmartPointerType::Shared;
    QString getter;
    QString refCountMethodName;
    QString valueCheckMethod;
    QString nullCheckMethod;
    QString resetMethod;
    TypeDatabaseParserContext::SmartPointerEntry entry;
    QString instantiations;
    QString excludedInstantiations;
    TypeSystem::SmartPointerToPythonConversion conversion =
        TypeSystem::SmartPointerToPythonConversion::Default;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"type") {
            const auto attribute = attributes->takeAt(i);
            const auto typeOpt = smartPointerTypeFromAttribute(attribute.value());
            if (!typeOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return nullptr;
            }
            smartPointerType = typeOpt.value();
        } else if (name == u"getter") {
            getter = attributes->takeAt(i).value().toString();
        } else if (name == u"ref-count-method") {
            refCountMethodName = attributes->takeAt(i).value().toString();
        } else if (name == u"instantiations") {
            entry.instantiations = attributes->takeAt(i).value().toString();
        } else if (name == u"excluded-instantiations") {
            entry.excludedInstantiations = attributes->takeAt(i).value().toString();
        } else if (name == u"value-check-method") {
            valueCheckMethod = attributes->takeAt(i).value().toString();
        } else if (name == u"null-check-method") {
            nullCheckMethod = attributes->takeAt(i).value().toString();
        } else if (name == u"reset-method") {
            resetMethod =  attributes->takeAt(i).value().toString();
        } else if (name == smartPointerToPythonConversionAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto convOpt = smartPointerToPythonConversionFromAttribute(attribute.value());
            if (!convOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return nullptr;
            }
            conversion = convOpt.value();
        }
    }

    if (getter.isEmpty()) {
        m_error = u"No function getter name specified for getting the raw pointer held by the smart pointer."_s;
        return nullptr;
    }

    QString signature = getter + u"()"_s;
    signature = TypeDatabase::normalizedSignature(signature);
    if (signature.isEmpty()) {
        m_error = u"No signature for the smart pointer getter found."_s;
        return nullptr;
    }

    QString errorString = checkSignatureError(signature,
                                              u"smart-pointer-type"_s);
    if (!errorString.isEmpty()) {
        m_error = errorString;
        return nullptr;
    }

    if (smartPointerType == TypeSystem::SmartPointerType::Unique && resetMethod.isEmpty()) {
        m_error = u"Unique pointers require a reset() method."_s;
        return nullptr;
    }

    auto type = std::make_shared<SmartPointerTypeEntry>(name, getter, smartPointerType,
                                                        refCountMethodName, since,
                                                        currentParentTypeEntry());
    if (!applyComplexTypeAttributes(reader, type, attributes))
        return nullptr;
    type->setToPythonConversion(conversion);
    type->setNullCheckMethod(nullCheckMethod);
    type->setValueCheckMethod(valueCheckMethod);
    type->setResetMethod(resetMethod);
    if (!entry.instantiations.isEmpty() || !entry.excludedInstantiations.isEmpty())
        m_context->smartPointerInstantiations.insert(type, entry);
    return type;
}

PrimitiveTypeEntryPtr
    TypeSystemParser::parsePrimitiveTypeEntry(const ConditionalStreamReader &reader,
                                     const QString &name, const QVersionNumber &since,
                                     QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    auto type = std::make_shared<PrimitiveTypeEntry>(name, since, currentParentTypeEntry());
    QString targetLangApiName;
    if (!applyCppAttributes(reader, type, attributes))
        return nullptr;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == targetLangNameAttribute) {
            type->setTargetLangName(attributes->takeAt(i).value().toString());
        } else if (name == u"target-lang-api-name") {
            targetLangApiName = attributes->takeAt(i).value().toString();
        } else if (name == preferredConversionAttribute) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == preferredTargetLangTypeAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          preferredTargetLangTypeAttribute, true);
            type->setPreferredTargetLangType(v);
        }
    }

    if (!targetLangApiName.isEmpty()) {
        auto e = m_context->db->findType(targetLangApiName);
        if (!e || !e->isCustom()) {
               m_error = msgInvalidTargetLanguageApiName(targetLangApiName);
               return nullptr;
        }
        type->setTargetLangApiType(std::static_pointer_cast<CustomTypeEntry>(e));
    }
    type->setTargetLangPackage(m_defaultPackage);
    return type;
}

// "int:QList_int;QString:QList_QString"
bool TypeSystemParser::parseOpaqueContainers(QStringView s, OpaqueContainers *result)
{
    const auto entries = s.split(u';');
    for (const auto &entry : entries) {
        const auto values = entry.split(u':');
        if (values.size() != 2) {
            m_error = u"Error parsing the opaque container attribute: \""_s
                      + s.toString() + u"\"."_s;
           return false;
        }
        OpaqueContainer oc;
        oc.name = values.at(1).trimmed().toString();
        const auto instantiations = values.at(0).split(u',', Qt::SkipEmptyParts);
        for (const auto &instantiationV : instantiations) {
           QString instantiation = instantiationV.trimmed().toString();
           // Fix to match AbstractMetaType::signature() which is used for matching
           // "Foo*" -> "Foo *"
           const auto asteriskPos = instantiation.indexOf(u'*');
           if (asteriskPos > 0 && !instantiation.at(asteriskPos - 1).isSpace())
                instantiation.insert(asteriskPos, u' ');
           oc.instantiations.append(instantiation);
        }
        result->append(oc);
    }
    return true;
}

ContainerTypeEntryPtr
    TypeSystemParser::parseContainerTypeEntry(const ConditionalStreamReader &reader,
                                     const QString &name, const QVersionNumber &since,
                                     QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    const auto typeIndex = indexOfAttribute(*attributes, u"type");
    if (typeIndex == -1) {
        m_error = u"no 'type' attribute specified"_s;
        return nullptr;
    }
    const auto typeName = attributes->at(typeIndex).value();
    const auto containerTypeOpt = containerTypeFromAttribute(typeName);
    if (!containerTypeOpt.has_value()) {
        m_error = u"there is no container of type "_s + typeName.toString();
        return nullptr;
    }
    attributes->removeAt(typeIndex);
    auto type = std::make_shared<ContainerTypeEntry>(name, containerTypeOpt.value(),
                                                     since, currentParentTypeEntry());
    if (!applyComplexTypeAttributes(reader, type, attributes))
        return nullptr;

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == opaqueContainerAttribute) {
            const auto attribute = attributes->takeAt(i);
            OpaqueContainers oc;
            if (!parseOpaqueContainers(attribute.value(), &oc))
                return nullptr;
            type->appendOpaqueContainers(oc);
        }
    }

    return type;
}

bool TypeSystemParser::parseOpaqueContainerElement(QXmlStreamAttributes *attributes)
{
    QString containerName;
    OpaqueContainers oc;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute) {
            containerName = attributes->takeAt(i).value().toString();
        } else if (name == opaqueContainerAttribute) {
            const auto attribute = attributes->takeAt(i);
            if (!parseOpaqueContainers(attribute.value(), &oc))
                return false;
        }
    }
    if (containerName.isEmpty()) {
        m_error = msgMissingAttribute(nameAttribute);
        return false;
    }
    m_context->opaqueContainerHash[containerName].append(oc);
    return true;
}

EnumTypeEntryPtr
    TypeSystemParser::parseEnumTypeEntry(const ConditionalStreamReader &reader,
                                const QString &name, const QVersionNumber &since,
                                QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    auto entry = std::make_shared<EnumTypeEntry>(name, since, currentParentTypeEntry());
    if (!applyCppAttributes(reader, entry, attributes))
        return nullptr;
    entry->setTargetLangPackage(m_defaultPackage);

    QString flagNames;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"upper-bound") {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == u"lower-bound") {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == docFileAttribute) {
            entry->setDocFile(attributes->takeAt(i).value().toString());
        } else if (name == forceUnsignedAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), forceUnsignedAttribute, false))
                entry->setForceUnsigned(true);
        } else if (name == forceIntegerAttribute) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == pythonEnumTypeAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto typeOpt = pythonEnumTypeFromAttribute(attribute.value());
            if (typeOpt.has_value()) {
                entry->setPythonEnumType(typeOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == cppEnumTypeAttribute) {
            entry->setCppType(attributes->takeAt(i).value().toString());
        } else if (name == extensibleAttribute) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == flagsAttribute) {
            flagNames = attributes->takeAt(i).value().toString();
        }
    }

    // put in the flags parallel...
    if (!flagNames.isEmpty()) {
        const QStringList &flagNameList = flagNames.split(u',');
        for (const QString &flagName : flagNameList)
            parseFlagsEntry(reader, entry, flagName.trimmed(), since, attributes);
    }
    return entry;
}


NamespaceTypeEntryPtr
    TypeSystemParser::parseNamespaceTypeEntry(const ConditionalStreamReader &reader,
                                     const QString &name, const QVersionNumber &since,
                                     QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    auto result = std::make_shared<NamespaceTypeEntry>(name, since, currentParentTypeEntry());
    auto visibility = TypeSystem::Visibility::Unspecified;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto attributeName = attributes->at(i).qualifiedName();
        if (attributeName == u"files") {
            const QString pattern = attributes->takeAt(i).value().toString();
            QRegularExpression re(pattern);
            if (!re.isValid()) {
                m_error = msgInvalidRegularExpression(pattern, re.errorString());
                return nullptr;
            }
            result->setFilePattern(re);
        } else if (attributeName == u"extends") {
            const auto extendsPackageName = attributes->at(i).value();
            auto allEntries = TypeDatabase::instance()->findNamespaceTypes(name);
            auto extendsIt = std::find_if(allEntries.cbegin(), allEntries.cend(),
                                          [extendsPackageName] (const NamespaceTypeEntryCPtr &e) {
                                              return e->targetLangPackage() == extendsPackageName;
                                          });
            if (extendsIt == allEntries.cend()) {
                m_error = msgCannotFindNamespaceToExtend(name, extendsPackageName.toString());
                return nullptr;
            }
            result->setExtends(*extendsIt);
            attributes->removeAt(i);
        } else if (attributeName == visibleAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto visibilityOpt = visibilityFromAttribute(attribute.value());
            if (!visibilityOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return nullptr;
            }
            visibility = visibilityOpt.value();
        } else if (attributeName == generateAttribute) {
            if (!convertBoolean(attributes->takeAt(i).value(), generateAttribute, true))
                visibility = TypeSystem::Visibility::Invisible;
        } else if (attributeName == generateUsingAttribute) {
            result->setGenerateUsing(convertBoolean(attributes->takeAt(i).value(),
                                                    generateUsingAttribute, true));
        }
    }

    if (visibility != TypeSystem::Visibility::Unspecified)
        result->setVisibility(visibility);
    // Handle legacy "generate" before the common handling
    if (!applyComplexTypeAttributes(reader, result, attributes))
        return {};

    if (result->extends() && !result->hasPattern()) {
        m_error = msgExtendingNamespaceRequiresPattern(name);
        return {};
    }

    return result;
}

ValueTypeEntryPtr
    TypeSystemParser::parseValueTypeEntry(const ConditionalStreamReader &reader,
                                 const QString &name, const QVersionNumber &since,
                                 QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    auto typeEntry = std::make_shared<ValueTypeEntry>(name, since, currentParentTypeEntry());
    if (!applyComplexTypeAttributes(reader, typeEntry, attributes))
        return nullptr;
    return typeEntry;
}

FunctionTypeEntryPtr
    TypeSystemParser::parseFunctionTypeEntry(const ConditionalStreamReader &reader,
                                    const QString &name, const QVersionNumber &since,
                                    QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;

    FunctionModification mod;
    const auto oldAttributesSize = attributes->size();
    if (!parseModifyFunctionAttributes(attributes, &mod))
        return nullptr;
    const bool hasModification = attributes->size() < oldAttributesSize;

    QString originalSignature;
    QString docFile;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == signatureAttribute)
            originalSignature = attributes->takeAt(i).value().toString().simplified();
        else if (name == docFileAttribute)
            docFile = attributes->takeAt(i).value().toString();
    }

    const QString signature = TypeDatabase::normalizedSignature(originalSignature);
    if (signature.isEmpty()) {
        m_error =  msgMissingAttribute(signatureAttribute);
        return nullptr;
    }

    if (hasModification) {
        mod.setOriginalSignature(originalSignature);
        mod.setSignature(signature);
        m_contextStack.top()->functionMods << mod;
    }

    TypeEntryPtr existingType = m_context->db->findType(name);

    if (!existingType) {
        auto result = std::make_shared<FunctionTypeEntry>(name, signature, since,
                                                          currentParentTypeEntry());
        result->setTargetLangPackage(m_defaultPackage);
        result->setDocFile(docFile);
        applyCommonAttributes(reader, result, attributes);
        return result;
    }

    if (existingType->type() != TypeEntry::FunctionType) {
        m_error = name + " expected to be a function, but isn't! Maybe it was already declared as a class or something else."_L1;
        return nullptr;
    }

    auto result = std::static_pointer_cast<FunctionTypeEntry>(existingType);
    result->addSignature(signature);
    return result;
}

TypedefEntryPtr
 TypeSystemParser::parseTypedefEntry(const ConditionalStreamReader &reader,
                                     const QString &name, StackElement topElement,
                                     const QVersionNumber &since,
                                     QXmlStreamAttributes *attributes)
{
    if (!checkRootElement())
        return nullptr;
    if (topElement != StackElement::Root
        && topElement != StackElement::NamespaceTypeEntry) {
        m_error = u"typedef entries must be nested in namespaces or type system."_s;
        return nullptr;
    }
    const auto sourceIndex = indexOfAttribute(*attributes, sourceAttribute);
    if (sourceIndex == -1) {
        m_error =  msgMissingAttribute(sourceAttribute);
        return nullptr;
    }
    const QString sourceType = attributes->takeAt(sourceIndex).value().toString();
    auto result = std::make_shared<TypedefEntry>(name, sourceType, since,
                                                 currentParentTypeEntry());
    if (!applyComplexTypeAttributes(reader, result, attributes))
        return nullptr;
    return result;
}

bool TypeSystemParser::applyComplexTypeAttributes(const ConditionalStreamReader &reader,
                                                  const ComplexTypeEntryPtr &ctype,
                                                  QXmlStreamAttributes *attributes)
{
    if (!applyCppAttributes(reader, ctype, attributes))
        return false;
    bool generate = true;
    auto exceptionHandling = m_exceptionHandling;
    auto allowThread = m_allowThread;

    QString package = m_defaultPackage;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == streamAttribute) {
            ctype->setStream(convertBoolean(attributes->takeAt(i).value(), streamAttribute, false));
        } else if (name == privateAttribute) {
            ctype->setPrivate(convertBoolean(attributes->takeAt(i).value(),
                                             privateAttribute, false));
        } else if (name == generateAttribute) {
            generate = convertBoolean(attributes->takeAt(i).value(), generateAttribute, true);
        } else if (name ==packageAttribute) {
            package = attributes->takeAt(i).value().toString();
        } else if (name == defaultSuperclassAttribute) {
            ctype->setDefaultSuperclass(attributes->takeAt(i).value().toString());
        } else if (name == genericClassAttribute) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          genericClassAttribute, false);
            ctype->setGenericClass(v);
        } else if (name == targetLangNameAttribute) {
            ctype->setTargetLangName(attributes->takeAt(i).value().toString());
        } else if (name == polymorphicBaseAttribute) {
            const bool v = convertBoolean(attributes->takeAt(i).value(),
                                          polymorphicBaseAttribute, false);
            ctype->setIsPolymorphicBase(v);
        } else if (name == u"polymorphic-name-function") {
            ctype->setPolymorphicNameFunction(attributes->takeAt(i).value().toString());
        } else if (name == u"polymorphic-id-expression") {
            ctype->setPolymorphicIdValue(attributes->takeAt(i).value().toString());
        } else if (name == exceptionHandlingAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto exceptionOpt = exceptionHandlingFromAttribute(attribute.value());
            if (exceptionOpt.has_value()) {
                exceptionHandling = exceptionOpt.value();
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == allowThreadAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto allowThreadOpt = allowThreadFromAttribute(attribute.value());
            if (allowThreadOpt.has_value()) {
                allowThread = allowThreadOpt.value();
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == u"held-type") {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        } else if (name == u"hash-function") {
            ctype->setHashFunction(attributes->takeAt(i).value().toString());
        } else if (name == forceAbstractAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), forceAbstractAttribute, false))
                ctype->setTypeFlags(ctype->typeFlags() | ComplexTypeEntry::ForceAbstract);
        } else if (name == deprecatedAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), deprecatedAttribute, false))
                ctype->setTypeFlags(ctype->typeFlags() | ComplexTypeEntry::Deprecated);
        } else if (name == disableWrapperAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), disableWrapperAttribute, false))
                ctype->setTypeFlags(ctype->typeFlags() | ComplexTypeEntry::DisableWrapper);
        } else if (name == deleteInMainThreadAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), deleteInMainThreadAttribute, false))
                ctype->setDeletionMode(TypeSystem::DeletionMode::DeleteInMainThread);
        } else if (name == deleteInOwnerThreadAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), deleteInOwnerThreadAttribute, false))
                ctype->setDeletionMode(TypeSystem::DeletionMode::DeleteInQObjectOwnerThread);
        } else if (name == qtMetaObjectFunctionsAttribute) {
            if (!convertBoolean(attributes->takeAt(i).value(),
                                qtMetaObjectFunctionsAttribute, true))  {
                ctype->setTypeFlags(ctype->typeFlags()
                                    | ComplexTypeEntry::DisableQtMetaObjectFunctions);
            }
        } else if (name == generateFunctionsAttribute) {
            const auto names = attributes->takeAt(i).value();
            const auto nameList = names.split(u';', Qt::SkipEmptyParts);
            QSet<QString> nameSet;
            for (const auto &name : nameList)
                nameSet.insert(name.trimmed().toString());
            ctype->setGenerateFunctions(nameSet);
        } else if (name == u"target-type") {
            ctype->setTargetType(attributes->takeAt(i).value().toString());
        }  else if (name == snakeCaseAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto snakeCaseOpt = snakeCaseFromAttribute(attribute.value());
            if (snakeCaseOpt.has_value()) {
                ctype->setSnakeCase(snakeCaseOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        }  else if (name == isNullAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto boolCastOpt = boolCastFromAttribute(attribute.value());
            if (boolCastOpt.has_value()) {
                ctype->setIsNullMode(boolCastOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        }  else if (name == operatorBoolAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto boolCastOpt = boolCastFromAttribute(attribute.value());
            if (boolCastOpt.has_value()) {
                ctype->setOperatorBoolMode(boolCastOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == parentManagementAttribute) {
            const auto attribute = attributes->takeAt(i);
            if (convertBoolean(attribute.value(), parentManagementAttribute, false))
                ctype->setTypeFlags(ctype->typeFlags() | ComplexTypeEntry::ParentManagement);
            ComplexTypeEntry::setParentManagementEnabled(true);
        }  else if (name == docFileAttribute) {
            ctype->setDocFile(attributes->takeAt(i).value().toString());
        }
    }

    if (exceptionHandling != TypeSystem::ExceptionHandling::Unspecified)
         ctype->setExceptionHandling(exceptionHandling);
    if (allowThread != TypeSystem::AllowThread::Unspecified)
        ctype->setAllowThread(allowThread);

    // The generator code relies on container's package being empty.
    if (ctype->type() != TypeEntry::ContainerType)
        ctype->setTargetLangPackage(package);

    if (generate)
        ctype->setCodeGeneration(m_generate);
    else
        ctype->setCodeGeneration(TypeEntry::GenerationDisabled);
    return true;
}

bool TypeSystemParser::parseConfiguration(StackElement topElement,
                                          QXmlStreamAttributes *attributes)
{
    if (!isComplexTypeEntry(topElement)
        && topElement != StackElement::EnumTypeEntry) {
        m_error = u"<configuration> must be nested into a complex or enum type entry."_s;
        return false;
    }
    QString condition;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"condition") {
            condition = attributes->takeAt(i).value().toString();
        }
    }
    if (condition.isEmpty()) {
        m_error = u"<configuration> requires a \"condition\" attribute."_s;
        return false;
    }
    const auto topEntry = m_contextStack.top()->entry;
    const auto configurableEntry = std::dynamic_pointer_cast<ConfigurableTypeEntry>(topEntry);
    Q_ASSERT(configurableEntry);
    configurableEntry->setConfigCondition(condition);
    return true;
}

bool TypeSystemParser::parseOverloadRemoval(StackElement topElement,
                                            QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::Root) {
        m_error = u"<overload-removal> can only appear under the root element."_s;
        return false;
    }
    OverloadRemovalRule rule;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"type") {
            rule.type = attributes->takeAt(i).value().toString();
        } else if (name == u"replaces") {
            rule.redundantTypes = attributes->takeAt(i).value().toString().split(u';');
        }
    }
    if (rule.type.isEmpty() || rule.redundantTypes.isEmpty()) {
        m_error = u"<overload-removal> requires \"type\" and \"replaces\" attributes."_s;
        return false;
    }
    TypeDatabase::instance()->addOverloadRemovalRule(rule);
    return true;
}

bool TypeSystemParser::parseRenameFunction(const ConditionalStreamReader &,
                                  QString *name, QXmlStreamAttributes *attributes)
{
    QString signature;
    QString rename;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == signatureAttribute) {
            // Do not remove as it is needed for the type entry later on
            signature = attributes->at(i).value().toString().simplified();
        } else if (name == renameAttribute) {
            rename = attributes->takeAt(i).value().toString();
        }
    }

    if (signature.isEmpty()) {
        m_error = msgMissingAttribute(signatureAttribute);
        return false;
    }

    *name = signature.left(signature.indexOf(u'(')).trimmed();

    QString errorString = checkSignatureError(signature, u"function"_s);
    if (!errorString.isEmpty()) {
        m_error = errorString;
        return false;
    }

    if (!rename.isEmpty()) {
        static const QRegularExpression functionNameRegExp(u"^[a-zA-Z_][a-zA-Z0-9_]*$"_s);
        Q_ASSERT(functionNameRegExp.isValid());
        if (!functionNameRegExp.match(rename).hasMatch()) {
            m_error = u"can not rename '"_s + signature + u"', '"_s
                      + rename + u"' is not a valid function name"_s;
            return false;
        }
        FunctionModification mod;
        if (!mod.setSignature(signature, &m_error))
            return false;
        mod.setRenamedToName(rename);
        mod.setModifierFlag(FunctionModification::Rename);
        m_contextStack.top()->functionMods << mod;
    }
    return true;
}

bool TypeSystemParser::parseInjectDocumentation(const ConditionalStreamReader &, StackElement topElement,
                                       QXmlStreamAttributes *attributes)
{
    const bool isAddFunction = topElement == StackElement::AddFunction;
    const bool validParent = isTypeEntry(topElement)
        || topElement == StackElement::ModifyFunction
        || topElement == StackElement::ModifyField
        || isAddFunction;
    if (!validParent) {
        m_error = u"inject-documentation must be inside modify-function, add-function"
                   "modify-field or other tags that creates a type"_s;
        return false;
    }

    TypeSystem::DocModificationMode mode = TypeSystem::DocModificationReplace;
    DocumentationFormat format = DocumentationFormat::Native;
    DocumentationEmphasis emphasis = DocumentationEmphasis::None;
    DocumentationTarget target = DocumentationTarget::Documentation;

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"mode") {
            const auto attribute = attributes->takeAt(i);
            const auto modeOpt = docModificationFromAttribute(attribute.value());
            if (!modeOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            mode = modeOpt.value();
        } else if (name == formatAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto formatOpt = documentationFormatFromAttribute(attribute.value());
            if (!formatOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            format = formatOpt.value();
        } else if (name == u"emphasis") {
            const auto attribute = attributes->takeAt(i);
            const auto emphasisOpt = documentationEmphasisFromAttribute(attribute.value());
            if (!emphasisOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            emphasis = emphasisOpt.value();
        } else if (name == u"target") {
            const auto attribute = attributes->takeAt(i);
            const auto targetOpt = docTargetFromAttribute(attribute.value());
            if (!targetOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            target = targetOpt.value();
        }
    }

    if (emphasis != DocumentationEmphasis::None && mode == TypeSystem::DocModificationXPathReplace) {
        m_error = "Emphasis is not supported for XPathReplace"_L1;
        return false;
    }

    if (target == DocumentationTarget::DocString && mode != TypeSystem::DocModificationReplace) {
        m_error = "Doc strings only support \"replace\""_L1;
        return false;
    }

    QString signature = isTypeEntry(topElement) ? QString() : m_currentSignature;
    DocModification mod(mode, signature);
    mod.setFormat(format);
    mod.setEmphasis(emphasis);
    mod.setTarget(target);
    if (hasFileSnippetAttributes(attributes)) {
        const auto snippetOptional = readFileSnippet(attributes);
        if (!snippetOptional.has_value())
            return false;
        mod.setCode(snippetOptional.value().content);
    }
    auto &top = m_contextStack.top();
    if (isAddFunction)
        top->addedFunctions.last()->addDocModification(mod);
    else
        top->docModifications << mod;
    return true;
}

bool TypeSystemParser::parseModifyDocumentation(const ConditionalStreamReader &,
                                       StackElement topElement,
                                       QXmlStreamAttributes *attributes)
{
    const bool validParent = isTypeEntry(topElement)
        || topElement == StackElement::ModifyFunction
        || topElement == StackElement::ModifyField;
    if (!validParent) {
        m_error = u"modify-documentation must be inside modify-function, "
                   "modify-field or other tags that creates a type"_s;
        return false;
    }

    const auto xpathIndex = indexOfAttribute(*attributes, xPathAttribute);
    if (xpathIndex == -1) {
        m_error = msgMissingAttribute(xPathAttribute);
        return false;
    }

    const QString xpath = attributes->takeAt(xpathIndex).value().toString();
    QString signature = isTypeEntry(topElement) ? QString() : m_currentSignature;
    m_contextStack.top()->docModifications
        << DocModification(xpath, signature);
    return true;
}

// m_exceptionHandling
TypeSystemTypeEntryPtr TypeSystemParser::parseRootElement(const ConditionalStreamReader &reader,
                                               const QVersionNumber &since,
                                               QXmlStreamAttributes *attributes)
{
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == packageAttribute) {
            const auto attribute = attributes->takeAt(i);
            if (!isValidPackageName(attribute.value())) {
                m_error = msgReaderError(reader, msgInvalidPackageName(attribute.value()));
                return {};
            }
            m_defaultPackage = attribute.value().toString();
        } else if (name == defaultSuperclassAttribute) {
            m_defaultSuperclass = attributes->takeAt(i).value().toString();
        } else if (name == exceptionHandlingAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto exceptionOpt = exceptionHandlingFromAttribute(attribute.value());
            if (exceptionOpt.has_value()) {
                m_exceptionHandling = exceptionOpt.value();
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == allowThreadAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto allowThreadOpt = allowThreadFromAttribute(attribute.value());
            if (allowThreadOpt.has_value()) {
                m_allowThread = allowThreadOpt.value();
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        }
    }

    if (m_defaultPackage.isEmpty()) { // Extending default, see addBuiltInContainerTypes()
        auto moduleEntry = std::const_pointer_cast<TypeSystemTypeEntry>(m_context->db->defaultTypeSystemType());
        if (!moduleEntry) {
            m_error = "No type system entry found (\"package\" attribute missing?)."_L1;
            return {};
        }
        m_defaultPackage = moduleEntry->name();
        return moduleEntry;
    }

    auto moduleEntry =
        std::const_pointer_cast<TypeSystemTypeEntry>(m_context->db->findTypeSystemType(m_defaultPackage));
    const bool add = !moduleEntry;
    if (add) {
        moduleEntry = std::make_shared<TypeSystemTypeEntry>(m_defaultPackage, since,
                                                            currentParentTypeEntry());
    }

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == docPackageAttribute) {
            moduleEntry->setDocTargetLangPackage(attributes->takeAt(i).value().toString());
        } else if (name == docModeAttribute) {
            const auto attribute = attributes->takeAt(i);
            auto docModeOpt = docModeFromAttribute(attribute.value());
            if (docModeOpt.has_value()) {
                  moduleEntry->setDocMode(docModeOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == snakeCaseAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto snakeCaseOpt = snakeCaseFromAttribute(attribute.value());
            if (snakeCaseOpt.has_value()) {
                moduleEntry->setSnakeCase(snakeCaseOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        } else if (name == subModuleOfAttribute) {
            moduleEntry->setSubModule(attributes->takeAt(i).value().toString());
        } else if (name == "namespace-begin"_L1) {
            moduleEntry->setNamespaceBegin(attributes->takeAt(i).value().toString());
        } else if (name == "namespace-end"_L1) {
            moduleEntry->setNamespaceEnd(attributes->takeAt(i).value().toString());
        } else if (name == usesGilAttribute) {
            moduleEntry->setUsesGil(convertBoolean(attributes->takeAt(i).value(),
                                                   usesGilAttribute, true));
        }
    }

    moduleEntry->setCodeGeneration(m_generate);

    if ((m_generate == TypeEntry::GenerateForSubclass ||
         m_generate == TypeEntry::GenerateNothing) && !m_defaultPackage.isEmpty())
        TypeDatabase::instance()->addRequiredTargetImport(m_defaultPackage);

    if (add)
        m_context->db->addTypeSystemType(moduleEntry);
    return moduleEntry;
}

bool TypeSystemParser::loadTypesystem(const ConditionalStreamReader &,
                             QXmlStreamAttributes *attributes)
{
    QString typeSystemName;
    bool generateChild = true;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute)
            typeSystemName = attributes->takeAt(i).value().toString();
        else if (name == generateAttribute)
           generateChild = convertBoolean(attributes->takeAt(i).value(), generateAttribute, true);
    }
    if (typeSystemName.isEmpty()) {
            m_error = u"No typesystem name specified"_s;
            return false;
    }
    const bool result =
        m_context->db->parseFile(m_context, typeSystemName, m_currentPath,
                                 generateChild && m_generate == TypeEntry::GenerateCode);
    if (!result)
        m_error = u"Failed to parse: '"_s + typeSystemName + u'\'';
    return result;
}

bool TypeSystemParser::parseRejectEnumValue(const ConditionalStreamReader &,
                                   QXmlStreamAttributes *attributes)
{
    if (!m_currentEnum) {
        m_error = u"<reject-enum-value> node must be used inside a <enum-type> node"_s;
        return false;
    }
    const auto nameIndex = indexOfAttribute(*attributes, nameAttribute);
    if (nameIndex == -1) {
        m_error = msgMissingAttribute(nameAttribute);
        return false;
    }
    m_currentEnum->addEnumValueRejection(attributes->takeAt(nameIndex).value().toString());
    return true;
}

bool TypeSystemParser::parseReplaceArgumentType(const ConditionalStreamReader &,
                                       StackElement topElement,
                                       QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"Type replacement can only be specified for argument modifications"_s;
        return false;
    }
    const auto modifiedTypeIndex = indexOfAttribute(*attributes, modifiedTypeAttribute);
    if (modifiedTypeIndex == -1) {
        m_error = u"Type replacement requires 'modified-type' attribute"_s;
        return false;
    }
    m_contextStack.top()->functionMods.last().argument_mods().last().setModifiedType(
        attributes->takeAt(modifiedTypeIndex).value().toString());
    return true;
}

bool TypeSystemParser::parseCustomConversion(const ConditionalStreamReader &,
                                    StackElement topElement,
                                    QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument
        && topElement != StackElement::ValueTypeEntry
        && topElement != StackElement::PrimitiveTypeEntry
        && topElement != StackElement::ContainerTypeEntry
        && topElement != StackElement::SmartPointerTypeEntry) {
        m_error = u"Conversion rules can only be specified for argument modification, "
                   "value-type, primitive-type, or container-type or smartpointer-type conversion."_s;
        return false;
    }

    QString sourceFile;
    QString snippetLabel;
    TypeSystem::Language lang = TypeSystem::NativeCode;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == classAttribute) {
            const auto languageAttribute = attributes->takeAt(i);
            const auto langOpt = languageFromAttribute(languageAttribute.value());
            if (!langOpt.has_value()) {
                m_error = msgInvalidAttributeValue(languageAttribute);
                return false;
            }
            lang = langOpt.value();
        } else if (name == u"file") {
            sourceFile = attributes->takeAt(i).value().toString();
        } else if (name == snippetAttribute) {
            snippetLabel = attributes->takeAt(i).value().toString();
        }
    }

    const auto &top = m_contextStack.top();
    if (topElement == StackElement::ModifyArgument) {
        CodeSnip snip;
        snip.language = lang;
        top->functionMods.last().argument_mods().last().conversionRules().append(snip);
        return true;
    }

    ValueTypeEntryPtr valueTypeEntry;
    if (top->entry->isValue()) {
        valueTypeEntry = std::static_pointer_cast<ValueTypeEntry>(top->entry);
        if (valueTypeEntry->hasTargetConversionRule() || valueTypeEntry->hasCustomConversion()) {
            m_error = u"Types can have only one conversion rule"_s;
            return false;
        }
    }

    // The old conversion rule tag that uses a file containing the conversion
    // will be kept temporarily for compatibility reasons. FIXME PYSIDE7: Remove
    if (valueTypeEntry != nullptr && !sourceFile.isEmpty()) {
        if (m_generate != TypeEntry::GenerateForSubclass
                && m_generate != TypeEntry::GenerateNothing) {
            qWarning(lcShiboken, "Specifying conversion rules by \"file\" is deprecated.");
            if (lang != TypeSystem::TargetLangCode)
                return true;

            const auto conversionRuleOptional = readFileSnippetContents(sourceFile, snippetLabel);
            if (!conversionRuleOptional.has_value())
                return false;
            valueTypeEntry->setTargetConversionRule(conversionRuleOptional.value());
        }
        return true;
    }

    auto customConversion = std::make_shared<CustomConversion>(top->entry);
    if (top->entry->isPrimitive())
        std::static_pointer_cast<PrimitiveTypeEntry>(top->entry)->setCustomConversion(customConversion);
    else if (top->entry->isContainer())
        std::static_pointer_cast<ContainerTypeEntry>(top->entry)->setCustomConversion(customConversion);
    else if (top->entry->isValue())
        std::static_pointer_cast<ValueTypeEntry>(top->entry)->setCustomConversion(customConversion);
    else if (top->entry->isSmartPointer())
        std::static_pointer_cast<SmartPointerTypeEntry>(top->entry)->setCustomConversion(customConversion);

    customConversionsForReview.append(customConversion);
    return true;
}

bool TypeSystemParser::parseNativeToTarget(const ConditionalStreamReader &,
                                  StackElement topElement,
                                  QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ConversionRule) {
        m_error = u"Native to Target conversion code can only be specified for custom conversion rules."_s;
        return false;
    }
    CodeSnip snip;
    if (!readCodeSnippet(attributes, &snip))
        return false;
    m_contextStack.top()->conversionCodeSnips.append(snip);
    return true;
}

bool TypeSystemParser::parseAddConversion(const ConditionalStreamReader &,
                                 StackElement topElement,
                                 QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::TargetToNative) {
        m_error = u"Target to Native conversions can only be added inside 'target-to-native' tags."_s;
        return false;
    }
    QString sourceTypeName;
    QString typeCheck;
    CodeSnip snip;
    if (!readCodeSnippet(attributes, &snip))
        return false;

    const auto &top = m_contextStack.top();
    top->conversionCodeSnips.append(snip);

    if (parserState() == ParserState::ArgumentTargetToNativeConversion)
        return true;

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"type")
             sourceTypeName = attributes->takeAt(i).value().toString();
        else if (name == u"check")
           typeCheck = attributes->takeAt(i).value().toString();
    }

    if (sourceTypeName.isEmpty()) {
        m_error = u"Target to Native conversions must specify the input type with the 'type' attribute."_s;
        return false;
    }
    auto customConversion = CustomConversion::getCustomConversion(top->entry);
    if (!customConversion) {
        m_error = msgMissingCustomConversion(top->entry);
        return false;
    }
    customConversion->addTargetToNativeConversion(sourceTypeName, typeCheck);
    return true;
}

static bool parseIndex(const QString &index, int *result, QString *errorMessage)
{
    bool ok = false;
    *result = index.toInt(&ok);
    if (!ok)
        *errorMessage = "Cannot convert '%1' to integer"_L1.arg(index);
    return ok;
}

static bool parseArgumentIndex(const QString &index, int *result, QString *errorMessage)
{
    if (index == u"return") {
        *result = 0;
        return true;
    }
    if (index == u"this") {
        *result = -1;
        return true;
    }
    return parseIndex(index, result, errorMessage);
}

bool TypeSystemParser::parseModifyArgument(const ConditionalStreamReader &,
                                  StackElement topElement, QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyFunction
        && topElement != StackElement::AddFunction
        && topElement != StackElement::DeclareFunction) {
        m_error = u"Argument modification requires <modify-function>,"
                  " <add-function> or <declare-function> as parent, was "_s
                  + tagFromElement(topElement).toString();
        return false;
    }

    QString index;
    QString renameTo;
    QString pyiType;
    bool resetAfterUse = false;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == indexAttribute) {
             index = attributes->takeAt(i).value().toString();
        } else if (name == invalidateAfterUseAttribute) {
            resetAfterUse = convertBoolean(attributes->takeAt(i).value(),
                                           invalidateAfterUseAttribute, false);
        } else if (name == renameAttribute) {
            renameTo = attributes->takeAt(i).value().toString();
        } else if (name == pyiTypeAttribute) {
            pyiType = attributes->takeAt(i).value().toString();
        }
    }

    if (index.isEmpty()) {
        m_error = msgMissingAttribute(indexAttribute);
        return false;
    }

    int idx = 0;
    if (!parseArgumentIndex(index, &idx, &m_error))
        return false;

    ArgumentModification argumentModification = ArgumentModification(idx);
    argumentModification.setResetAfterUse(resetAfterUse);
    argumentModification.setRenamedToName(renameTo);
    argumentModification.setPyiType(pyiType);
    m_contextStack.top()->functionMods.last().argument_mods().append(argumentModification);
    return true;
}

bool TypeSystemParser::parseNoNullPointer(const ConditionalStreamReader &reader,
                                 StackElement topElement, QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"no-null-pointer requires argument modification as parent"_s;
        return false;
    }

    ArgumentModification &lastArgMod = m_contextStack.top()->functionMods.last().argument_mods().last();
    lastArgMod.setNoNullPointers(true);

    const auto defaultValueIndex = indexOfAttribute(*attributes, u"default-value");
    if (defaultValueIndex != -1) {
        const QXmlStreamAttribute attribute = attributes->takeAt(defaultValueIndex);
        qCWarning(lcShiboken, "%s",
                  qPrintable(msgUnimplementedAttributeWarning(reader, attribute)));
    }
    return true;
}

bool TypeSystemParser::parseDefineOwnership(const ConditionalStreamReader &,
                                   StackElement topElement,
                                   QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"define-ownership requires argument modification as parent"_s;
        return false;
    }

    TypeSystem::Language lang = TypeSystem::TargetLangCode;
    std::optional<TypeSystem::Ownership> ownershipOpt;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == classAttribute) {
            const auto classAttribute = attributes->takeAt(i);
            const auto langOpt = languageFromAttribute(classAttribute.value());
            if (!langOpt.has_value() || langOpt.value() == TypeSystem::ShellCode) {
                m_error = msgInvalidAttributeValue(classAttribute);
                return false;
            }
            lang = langOpt.value();
        } else if (name == ownershipAttribute) {
            const auto attribute = attributes->takeAt(i);
            ownershipOpt = ownershipFromFromAttribute(attribute.value());
            if (!ownershipOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
        }
    }

    if (!ownershipOpt.has_value()) {
        m_error = "unspecified ownership"_L1;
        return false;
    }
    auto &lastArgMod = m_contextStack.top()->functionMods.last().argument_mods().last();
    switch (lang) {
    case TypeSystem::TargetLangCode:
        lastArgMod.setTargetOwnerShip(ownershipOpt.value());
        break;
    case TypeSystem::NativeCode:
        lastArgMod.setNativeOwnership(ownershipOpt.value());
        break;
    default:
        break;
    }
    return true;
}

// ### fixme PySide7: remove (replaced by attribute).
bool TypeSystemParser::parseRename(const ConditionalStreamReader &,
                          StackElement topElement,
                          QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"Argument modification parent required"_s;
        return false;
    }

    const auto toIndex = indexOfAttribute(*attributes, toAttribute);
    if (toIndex == -1) {
        m_error = msgMissingAttribute(toAttribute);
        return false;
    }
    const QString renamed_to = attributes->takeAt(toIndex).value().toString();
    m_contextStack.top()->functionMods.last().argument_mods().last().setRenamedToName(renamed_to);
    return true;
}

bool TypeSystemParser::parseModifyField(const ConditionalStreamReader &,
                                        QXmlStreamAttributes *attributes)
{
    FieldModification fm;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute) {
            fm.setName(attributes->takeAt(i).value().toString());
        } else if (name == removeAttribute) {
            fm.setRemoved(convertRemovalAttribute(attributes->takeAt(i).value()));
        } else if (name == opaqueContainerFieldAttribute) {
            fm.setOpaqueContainer(convertBoolean(attributes->takeAt(i).value(),
                                                 opaqueContainerFieldAttribute, false));
        }  else if (name == readAttribute) {
            fm.setReadable(convertBoolean(attributes->takeAt(i).value(), readAttribute, true));
        } else if (name == writeAttribute) {
            fm.setWritable(convertBoolean(attributes->takeAt(i).value(), writeAttribute, true));
        } else if (name == renameAttribute) {
            fm.setRenamedToName(attributes->takeAt(i).value().toString());
        } else if (name == snakeCaseAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto snakeCaseOpt = snakeCaseFromAttribute(attribute.value());
            if (snakeCaseOpt.has_value()) {
                fm.setSnakeCase(snakeCaseOpt.value());
            } else {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgInvalidAttributeValue(attribute)));
            }
        }
    }
    if (fm.name().isEmpty()) {
        m_error = msgMissingAttribute(nameAttribute);
        return false;
    }
    m_contextStack.top()->fieldMods << fm;
    return true;
}

static bool parseOverloadNumber(const QXmlStreamAttribute &attribute, int *overloadNumber,
                                QString *errorMessage)
{
    bool ok{};
    *overloadNumber = attribute.value().toInt(&ok);
    if (!ok || *overloadNumber < 0) {
        *errorMessage = msgInvalidAttributeValue(attribute);
        return false;
    }
    return true;
}

bool TypeSystemParser::parseAddFunction(const ConditionalStreamReader &,
                                        StackElement topElement,
                                        StackElement t,
                                        QXmlStreamAttributes *attributes)
{
    const bool validParent = isComplexTypeEntry(topElement)
        || topElement == StackElement::Root
        || topElement ==  StackElement::ContainerTypeEntry;
    if (!validParent) {
        m_error = "Add/Declare function requires a complex/container type or a root tag as parent, was=%1"_L1
                  + tagFromElement(topElement);
        return false;
    }

    FunctionModification mod;
    if (!(t == StackElement::AddFunction
          ? parseBasicModifyFunctionAttributes(attributes, &mod)
          : parseModifyFunctionAttributes(attributes, &mod))) {
        return false;
    }

    QString originalSignature;
    QString docSignature;
    QString returnType;
    bool staticFunction = false;
    bool classMethod = false;
    bool pythonOverride = false;
    QString access;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == signatureAttribute) {
            originalSignature = attributes->takeAt(i).value().toString().simplified();
        } else if (name == u"doc-signature") {
            docSignature = attributes->takeAt(i).value().toString().simplified();
        } else if (name == u"return-type") {
            returnType = attributes->takeAt(i).value().toString();
        } else if (name == staticAttribute) {
            staticFunction = convertBoolean(attributes->takeAt(i).value(),
                                            staticAttribute, false);
        } else if (name == classmethodAttribute) {
            classMethod = convertBoolean(attributes->takeAt(i).value(),
                                            classmethodAttribute, false);
        } else if (name == accessAttribute) {
            access = attributes->takeAt(i).value().toString();
        } else if (name == pythonOverrideAttribute) {
            pythonOverride = convertBoolean(attributes->takeAt(i).value(),
                                            pythonOverrideAttribute, false);
        }
    }

    QString signature = TypeDatabase::normalizedAddedFunctionSignature(originalSignature);
    if (signature.isEmpty()) {
        m_error = u"No signature for the added function"_s;
        return false;
    }

    QString errorString = checkSignatureError(signature, u"add-function"_s);
    if (!errorString.isEmpty()) {
        m_error = errorString;
        return false;
    }

    AddedFunctionPtr func = AddedFunction::createAddedFunction(signature, returnType, &errorString);
    if (!func) {
        m_error = errorString;
        return false;
    }

    func->setStatic(staticFunction);
    func->setClassMethod(classMethod);
    func->setDocSignature(docSignature);
    func->setPythonOverride(pythonOverride);
    func->setTargetLangPackage(m_defaultPackage);

    // Create signature for matching modifications
    signature = TypeDatabase::normalizedSignature(originalSignature);
    if (!signature.contains(u'('))
        signature += u"()"_s;
    m_currentSignature = signature;

    if (!access.isEmpty()) {
        const auto acessOpt = addedFunctionAccessFromAttribute(access);
        if (!acessOpt.has_value()) {
            m_error = u"Bad access type '"_s + access + u'\'';
            return false;
        }
        func->setAccess(acessOpt.value());
    }
    func->setDeclaration(t == StackElement::DeclareFunction);

    m_contextStack.top()->addedFunctions << func;
    m_contextStack.top()->addedFunctionModificationIndex =
        m_contextStack.top()->functionMods.size();

    if (!mod.setSignature(m_currentSignature, &m_error))
        return false;
    mod.setOriginalSignature(originalSignature);
    m_contextStack.top()->functionMods << mod;
    return true;
}

bool TypeSystemParser::parseAddPyMethodDef(const ConditionalStreamReader &,
                                           StackElement topElement,
                                           QXmlStreamAttributes *attributes)
{
    if (!isComplexTypeEntry(topElement)) {
        m_error = u"add-pymethoddef requires a complex type as parent, was="_s
                  + tagFromElement(topElement).toString();
        return false;
    }

    TypeSystemPyMethodDefEntry def;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute) {
            def.name = attributes->takeAt(i).value().toString();
        } else if (name == u"doc") {
            def.doc = attributes->takeAt(i).value().toString();
        } else if (name == u"function") {
            def.function = attributes->takeAt(i).value().toString();
        } else if (name == u"flags") {
            auto attribute = attributes->takeAt(i).value();
            std::optional<PyMethodFlags> flagsO = pyMethodFlagsFromString(attribute);
            if (!flagsO.has_value()) {
                m_error = "Invalid method flags: "_L1 + attribute.toString();
                return false;
            }
            def.flags = flagsO.value();
        } else if (name == u"signatures") {
            auto attribute = attributes->takeAt(i);
            const auto signatures = attribute.value().split(u';', Qt::SkipEmptyParts);
            for (const auto &signature : signatures)
                def.signatures.append(signature.toString());
        }
    }

    if (def.name.isEmpty() || def.function.isEmpty()) {
        m_error = u"add-pymethoddef requires at least a name and a function attribute"_s;
        return false;
    }
    std::static_pointer_cast<ComplexTypeEntry>(m_contextStack.top()->entry)->addPyMethodDef(def);
    return true;
}

bool TypeSystemParser::parseProperty(const ConditionalStreamReader &, StackElement topElement,
                                     QXmlStreamAttributes *attributes)
{
    if (!isComplexTypeEntry(topElement)) {
        m_error = "Add property requires a complex type as parent, was=%1"_L1
                  + tagFromElement(topElement);
        return false;
    }

    TypeSystemProperty property;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute) {
            property.name = attributes->takeAt(i).value().toString();
        } else if (name == u"get") {
            property.read = attributes->takeAt(i).value().toString();
        } else if (name == u"type") {
            property.type = attributes->takeAt(i).value().toString();
        } else if (name == u"set") {
            property.write = attributes->takeAt(i).value().toString();
        } else if (name == generateGetSetDefAttribute) {
            property.generateGetSetDef =
                convertBoolean(attributes->takeAt(i).value(),
                               generateGetSetDefAttribute, false);
        }
    }
    if (!property.isValid()) {
        m_error = u"<property> element is missing required attibutes (name/type/get)."_s;
        return false;
    }
    std::static_pointer_cast<ComplexTypeEntry>(m_contextStack.top()->entry)->addProperty(property);
    return true;
}

// Parse basic attributes applicable to <add-function>/<declare-function>/<function>
// and <modify-function> (all that is not done by injected code).
bool TypeSystemParser::parseBasicModifyFunctionAttributes(QXmlStreamAttributes *attributes,
                                                          FunctionModification *mod)
{
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == overloadNumberAttribute) {
            int overloadNumber = TypeSystem::OverloadNumberUnset;
            if (!parseOverloadNumber(attributes->takeAt(i), &overloadNumber, &m_error))
                return false;
            mod->setOverloadNumber(overloadNumber);
        } else if (name == deprecatedAttribute) {
            const bool deprecated = convertBoolean(attributes->takeAt(i).value(),
                                                   deprecatedAttribute, false);
            mod->setModifierFlag(deprecated ? FunctionModification::Deprecated
                                            : FunctionModification::Undeprecated);
        } else if (name == blockingAttribute) {
            if (convertBoolean(attributes->takeAt(i).value(), blockingAttribute, false))
                mod->setModifierFlag(FunctionModification::Blocking);
        }
    }
    return true;
}

// Parse attributes applicable to <declare-function>/<function>
// and <modify-function>.
bool TypeSystemParser::parseModifyFunctionAttributes(QXmlStreamAttributes *attributes,
                                                     FunctionModification *mod)
{
    if (!parseBasicModifyFunctionAttributes(attributes, mod))
        return false;

    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == allowThreadAttribute) {
            const QXmlStreamAttribute attribute = attributes->takeAt(i);
            const auto allowThreadOpt = allowThreadFromAttribute(attribute.value());
            if (!allowThreadOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            mod->setAllowThread(allowThreadOpt.value());
        } else if (name == exceptionHandlingAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto exceptionOpt = exceptionHandlingFromAttribute(attribute.value());
            if (!exceptionOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            mod->setExceptionHandling(exceptionOpt.value());
        } else if (name == snakeCaseAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto snakeCaseOpt = snakeCaseFromAttribute(attribute.value());
            if (!snakeCaseOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            mod->setSnakeCase(snakeCaseOpt.value());
        }
    }
    return true;
}

bool TypeSystemParser::parseModifyFunction(const ConditionalStreamReader &reader,
                                  StackElement topElement,
                                  QXmlStreamAttributes *attributes)
{
    const bool validParent = isComplexTypeEntry(topElement)
        || topElement == StackElement::TypedefTypeEntry
        || topElement == StackElement::FunctionTypeEntry;
    if (!validParent) {
        m_error = "Modify function requires complex type as parent, was=%1"_L1
                  + tagFromElement(topElement);
        return false;
    }

    QString originalSignature;
    FunctionModification mod;
    if (!parseModifyFunctionAttributes(attributes, &mod))
        return false;

    QString access;
    bool removed = false;
    QString rename;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == signatureAttribute) {
            originalSignature = attributes->takeAt(i).value().toString().simplified();
        } else if (name == accessAttribute) {
            access = attributes->takeAt(i).value().toString();
        } else if (name == renameAttribute) {
            rename = attributes->takeAt(i).value().toString();
        } else if (name == removeAttribute) {
            removed = convertRemovalAttribute(attributes->takeAt(i).value());
        } else if (name == virtualSlotAttribute || name == threadAttribute) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeWarning(reader, name)));
        }
    }

    // Child of global <function>
    const auto &top = m_contextStack.top();
    if (originalSignature.isEmpty() && top->entry->isFunction()) {
        auto f = std::static_pointer_cast<const FunctionTypeEntry>(top->entry);
        originalSignature = f->signatures().value(0);
    }

    const QString signature = TypeDatabase::normalizedSignature(originalSignature);
    if (signature.isEmpty()) {
        m_error = u"No signature for modified function"_s;
        return false;
    }

    QString errorString = checkSignatureError(signature, u"modify-function"_s);
    if (!errorString.isEmpty()) {
        m_error = errorString;
        return false;
    }

    if (!mod.setSignature(signature, &m_error))
        return false;
    mod.setOriginalSignature(originalSignature);
    m_currentSignature = signature;

    if (!access.isEmpty()) {
        const auto modifierFlagOpt = modifierFromAttribute(access);
        if (!modifierFlagOpt.has_value()) {
            m_error = u"Bad access type '"_s + access + u'\'';
            return false;
        }
        const FunctionModification::ModifierFlag m = modifierFlagOpt.value();
        if (m == FunctionModification::NonFinal) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnimplementedAttributeValueWarning(reader,
                      accessAttribute, access)));
        }
        mod.setModifierFlag(m);
    }

    mod.setRemoved(removed);

    if (!rename.isEmpty()) {
        mod.setRenamedToName(rename);
        mod.setModifierFlag(FunctionModification::Rename);
    }

    top->functionMods << mod;
    return true;
}

bool TypeSystemParser::parseReplaceDefaultExpression(const ConditionalStreamReader &,
                                            StackElement topElement,
                                            QXmlStreamAttributes *attributes)
{
    if (!(topElement & StackElement::ModifyArgument)) {
        m_error = u"Replace default expression only allowed as child of argument modification"_s;
        return false;
    }
    const auto withIndex = indexOfAttribute(*attributes, u"with");
    if (withIndex == -1 || attributes->at(withIndex).value().isEmpty()) {
        m_error = u"Default expression replaced with empty string. Use remove-default-expression instead."_s;
        return false;
    }

    m_contextStack.top()->functionMods.last().argument_mods().last().setReplacedDefaultExpression(
        attributes->takeAt(withIndex).value().toString());
    return true;
}

bool TypeSystemParser::parseReferenceCount(const ConditionalStreamReader &reader,
                                  StackElement topElement,
                                  QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"reference-count must be child of modify-argument"_s;
        return false;
    }

    ReferenceCount rc;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == actionAttribute) {
            const QXmlStreamAttribute attribute = attributes->takeAt(i);
            const auto actionOpt = referenceCountFromAttribute(attribute.value());
            if (!actionOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            rc.action = actionOpt.value();
            switch (rc.action) {
            case ReferenceCount::AddAll:
            case ReferenceCount::Ignore:
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgUnimplementedAttributeValueWarning(reader, attribute)));
                break;
            default:
                break;
            }
        } else if (name == u"variable-name") {
            rc.varName = attributes->takeAt(i).value().toString();
        }
    }

    m_contextStack.top()->functionMods.last().argument_mods().last().addReferenceCount(rc);
    return true;
}

bool TypeSystemParser::parseParentOwner(const ConditionalStreamReader &,
                               StackElement topElement,
                               QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::ModifyArgument) {
        m_error = u"parent-policy must be child of modify-argument"_s;
        return false;
    }
    ArgumentOwner ao;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == indexAttribute) {
            const QString index = attributes->takeAt(i).value().toString();
            if (!parseArgumentIndex(index, &ao.index, &m_error))
                return false;
        } else if (name == actionAttribute) {
            const auto action = attributes->takeAt(i);
            const auto actionOpt = argumentOwnerActionFromAttribute(action.value());
            if (!actionOpt.has_value()) {
                m_error = msgInvalidAttributeValue(action);
                return false;
            }
            ao.action = actionOpt.value();
        }
    }
    m_contextStack.top()->functionMods.last().argument_mods().last().setOwner(ao);
    return true;
}

std::optional<QString>
    TypeSystemParser::readFileSnippetContents(const QString &fileName,
                                              const QString &snippetName)
{
    static FileCache cache;

    const auto result = snippetName.isEmpty() ? cache.fileContents(fileName)
        : cache.fileSnippet(fileName, snippetName, snippetPattern(snippetName));

    if (!result.has_value())
        m_error = cache.errorString();
    return result;
}

std::optional<TypeSystemParser::Snippet>
    TypeSystemParser::readFileSnippet(QXmlStreamAttributes *attributes)
{
    Snippet result;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == fileAttribute) {
            result.fileName = attributes->takeAt(i).value().toString();
        } else if (name == snippetAttribute) {
            result.snippetLabel = attributes->takeAt(i).value().toString();
        }
    }
    if (result.fileName.isEmpty()) {
        m_error = "Snippet missing file name"_L1;
        return std::nullopt;
    }
    const QString resolved = m_context->db->modifiedTypesystemFilepath(result.fileName,
                                                                       m_currentPath);
    auto snippetO = readFileSnippetContents(resolved, result.snippetLabel);
    if (!snippetO.has_value())
        return std::nullopt;
    result.content = snippetO.value();
    return result;
}

bool TypeSystemParser::readCodeSnippet(QXmlStreamAttributes *attributes, CodeSnip *snip)
{
    if (!hasFileSnippetAttributes(attributes))
        return true; // Expecting inline content.
    const auto snippetOptional = readFileSnippet(attributes);
    if (!snippetOptional.has_value())
        return false;
    const auto &snippet = snippetOptional.value();

    QString source = snippet.fileName;
    if (!snippet.snippetLabel.isEmpty())
        source += " ("_L1 + snippet.snippetLabel + u')';
    QString content;
    QTextStream str(&content);
    str << "// ========================================================================\n"
           "// START of custom code block [file: "
        << source << "]\n" << snippet.content
        << "// END of custom code block [file: " << source
        << "]\n// ========================================================================\n";
    snip->addCode(content);
    return true;
}

bool TypeSystemParser::parseInjectCode(const ConditionalStreamReader &,
                              StackElement topElement,
                              QXmlStreamAttributes *attributes)
{
    if (!isComplexTypeEntry(topElement)
        && (topElement != StackElement::AddFunction)
        && (topElement != StackElement::ModifyFunction)
        && (topElement != StackElement::Root)) {
        m_error = u"wrong parent type for code injection"_s;
        return false;
    }

    TypeSystem::CodeSnipPosition position = TypeSystem::CodeSnipPositionBeginning;
    TypeSystem::Language lang = TypeSystem::TargetLangCode;
    CodeSnip snip;
    if (!readCodeSnippet(attributes, &snip))
        return false;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == classAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto langOpt = languageFromAttribute(attribute.value());
            if (!langOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            lang = langOpt.value();
        } else if (name == positionAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto positionOpt = codeSnipPositionFromAttribute(attribute.value());
            if (!positionOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            position = positionOpt.value();
        }
    }

    snip.position = position;
    snip.language = lang;

    switch (topElement) {
    case StackElement::ModifyFunction:
    case StackElement::AddFunction: {
        FunctionModification &mod = m_contextStack.top()->functionMods.last();
        mod.appendSnip(snip);
        if (!snip.code().isEmpty())
            mod.setModifierFlag(FunctionModification::CodeInjection);
    }
    break;
    case StackElement::Root:
        std::static_pointer_cast<TypeSystemTypeEntry>(m_contextStack.top()->entry)->addCodeSnip(snip);
        break;
    default:
        std::static_pointer_cast<ComplexTypeEntry>(m_contextStack.top()->entry)->addCodeSnip(snip);
        break;
    }
    return true;
}

bool TypeSystemParser::parseInclude(const ConditionalStreamReader &,
                           StackElement topElement,
                           const TypeEntryPtr &entry, QXmlStreamAttributes *attributes)
{
    QString fileName;
    Include::IncludeType location = Include::IncludePath;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == fileNameAttribute) {
            fileName = attributes->takeAt(i).value().toString();
        } else if (name == locationAttribute) {
            const auto attribute = attributes->takeAt(i);
            const auto locationOpt = locationFromAttribute(attribute.value());
            if (!locationOpt.has_value()) {
                m_error = msgInvalidAttributeValue(attribute);
                return false;
            }
            location = locationOpt.value();
        }
    }

    Include inc(location, fileName);
    if (isComplexTypeEntry(topElement)
        || topElement == StackElement::PrimitiveTypeEntry
        || topElement == StackElement::ContainerTypeEntry
        || topElement == StackElement::SmartPointerTypeEntry
        || topElement == StackElement::TypedefTypeEntry) {
        entry->setInclude(inc);
    } else if (topElement == StackElement::ExtraIncludes) {
        entry->addExtraInclude(inc);
    } else {
        m_error = u"Only supported parent tags are primitive-type, complex types or extra-includes"_s;
        return false;
    }
    return true;
}

bool TypeSystemParser::parseSystemInclude(const ConditionalStreamReader &,
                                          QXmlStreamAttributes *attributes)
{
    const auto index = indexOfAttribute(*attributes, fileNameAttribute);
    if (index == -1) {
        m_error = msgMissingAttribute(fileNameAttribute);
        return false;
    }
    TypeDatabase::instance()->addForceProcessSystemInclude(attributes->takeAt(index).value().toString());
    return true;
}

TemplateEntryPtr TypeSystemParser::parseTemplate(QXmlStreamAttributes *attributes)
{
    auto result = std::make_shared<TemplateEntry>();
    if (hasFileSnippetAttributes(attributes)) {
        const auto snippetOptional = readFileSnippet(attributes);
        if (!snippetOptional.has_value())
            return {};
        result->addCode(snippetOptional.value().content);
    }
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == nameAttribute)
            result->setName(attributes->takeAt(i).value().toString());
    }
    if (result->name().isEmpty()) {
        m_error = msgMissingAttribute(nameAttribute);
        return {};
    }
    return result;
}

std::optional<TemplateInstance>
    TypeSystemParser::parseInsertTemplate(const ConditionalStreamReader &,
                                          StackElement topElement,
                                          QXmlStreamAttributes *attributes)
{
    if ((topElement != StackElement::InjectCode) &&
        (topElement != StackElement::Template) &&
        (topElement != StackElement::NativeToTarget) &&
        (topElement != StackElement::AddConversion) &&
        (topElement != StackElement::ConversionRule)) {
        m_error = u"Can only insert templates into code snippets, templates, "\
                   "conversion-rule, native-to-target or add-conversion tags."_s;
        return std::nullopt;
    }
    const auto nameIndex = indexOfAttribute(*attributes, nameAttribute);
    if (nameIndex == -1) {
        m_error = msgMissingAttribute(nameAttribute);
        return std::nullopt;
    }
    return TemplateInstance(attributes->takeAt(nameIndex).value().toString());
}

bool TypeSystemParser::parseReplace(const ConditionalStreamReader &,
                           StackElement topElement, QXmlStreamAttributes *attributes)
{
    if (topElement != StackElement::InsertTemplate || !m_templateInstance.has_value()) {
        m_error = u"Can only insert replace rules into insert-template."_s;
        return false;
    }
    QString from;
    QString to;
    for (auto i = attributes->size() - 1; i >= 0; --i) {
        const auto name = attributes->at(i).qualifiedName();
        if (name == u"from")
            from = attributes->takeAt(i).value().toString();
        else if (name == toAttribute)
            to = attributes->takeAt(i).value().toString();
    }
    m_templateInstance->addReplaceRule(from, to);
    return true;
}

// Check for a duplicated type entry and return whether to add the new one.
// We need to be able to have duplicate primitive type entries,
// or it's not possible to cover all primitive target language
// types (which we need to do in order to support fake meta objects)
bool TypeSystemParser::checkDuplicatedTypeEntry(const ConditionalStreamReader &reader,
                                                StackElement t,
                                                const QString &name) const
{
    if (t == StackElement::PrimitiveTypeEntry || t == StackElement::FunctionTypeEntry)
        return true;
    const auto duplicated = m_context->db->findType(name);
    if (!duplicated || duplicated->isNamespace())
        return true;
    if (duplicated->isBuiltIn()) {
        qCWarning(lcShiboken, "%s",
                  qPrintable(msgReaderMessage(reader, "Warning",
                                              msgDuplicateBuiltInTypeEntry(name))));
        return false;
    }
    qCWarning(lcShiboken, "%s",
              qPrintable(msgReaderMessage(reader, "Warning",
                                          msgDuplicateTypeEntry(name))));
    return true;
}

static bool parseVersion(const QString &versionSpec, const QString &package,
                         QVersionNumber *result, QString *errorMessage)
{
    *result = QVersionNumber::fromString(versionSpec);
    if (result->isNull()) {
        *errorMessage = msgInvalidVersion(versionSpec, package);
        return false;
    }
    return true;
}

bool TypeSystemParser::startElement(const ConditionalStreamReader &reader, StackElement element)
{
    if (m_ignoreDepth) {
        ++m_ignoreDepth;
        return true;
    }

    const auto tagName = reader.name();
    QXmlStreamAttributes attributes = reader.attributes();

    VersionRange versionRange;
    for (auto i = attributes.size() - 1; i >= 0; --i) {
        const auto name = attributes.at(i).qualifiedName();
        if (name == sinceAttribute) {
            if (!parseVersion(attributes.takeAt(i).value().toString(),
                              m_defaultPackage, &versionRange.since, &m_error)) {
                return false;
            }
        } else if (name == untilAttribute) {
            if (!parseVersion(attributes.takeAt(i).value().toString(),
                              m_defaultPackage, &versionRange.until, &m_error)) {
                return false;
            }
        }
    }

    if (!m_defaultPackage.isEmpty() && !versionRange.isNull()) {
        if (!TypeDatabase::checkApiVersion(m_defaultPackage, versionRange)) {
            ++m_ignoreDepth;
            return true;
        }
    }

    if (element == StackElement::ImportFile)
        return importFileElement(attributes);

    if (m_currentDroppedEntryDepth) {
        ++m_currentDroppedEntryDepth;
        return true;
    }

    if (element == StackElement::Root && m_generate == TypeEntry::GenerateCode)
        customConversionsForReview.clear();

    if (element == StackElement::Unimplemented) {
        qCWarning(lcShiboken, "%s",
                  qPrintable(msgUnimplementedElementWarning(reader, tagName)));
        return true;
    }

    if (isTypeEntry(element) || element == StackElement::Root)
        m_contextStack.push(std::make_shared<StackElementContext>());

    if (m_contextStack.isEmpty()) {
        m_error = msgNoRootTypeSystemEntry();
        return false;
    }

    const auto &top = m_contextStack.top();
    const StackElement topElement = m_stack.value(m_stack.size() - 2, StackElement::None);

    if (isTypeEntry(element)) {
        QString name;
        if (element != StackElement::FunctionTypeEntry) {
            const auto nameIndex = indexOfAttribute(attributes, nameAttribute);
            if (nameIndex != -1) {
                name = attributes.takeAt(nameIndex).value().toString();
                if (element != StackElement::CustomTypeEntry
                    && element != StackElement::PrimitiveTypeEntry
                    && !isValidClassName(name)) {
                    m_error = msgReaderError(reader, msgInvalidTypeName(name));
                    return false;
                }
            } else if (element != StackElement::EnumTypeEntry) { // anonymous enum?
                m_error = msgMissingAttribute(nameAttribute);
                return false;
            }
        }
        // Allow for primitive and/or std:: types only, else require proper nesting.
        if (element != StackElement::PrimitiveTypeEntry && name.contains(u':')
            && !name.contains(u"std::")) {
            m_error = msgIncorrectlyNestedName(name);
            return false;
        }

        if (m_context->db->hasDroppedTypeEntries()) {
            const QString identifier = element == StackElement::FunctionTypeEntry
                ? attributes.value(signatureAttribute).toString().simplified() : name;
            if (shouldDropTypeEntry(m_context->db, m_contextStack, identifier)) {
                m_currentDroppedEntryDepth = 1;
                if (ReportHandler::isDebug(ReportHandler::SparseDebug)) {
                    qCInfo(lcShiboken, "Type system entry '%s' was intentionally dropped from generation.",
                           qPrintable(identifier));
                }
                 m_contextStack.pop();
                return true;
            }
        }

        // The top level tag 'function' has only the 'signature' tag
        // and we should extract the 'name' value from it.
        if (element == StackElement::FunctionTypeEntry
            && !parseRenameFunction(reader, &name, &attributes)) {
                return false;
        }

        // We need to be able to have duplicate primitive type entries,
        // or it's not possible to cover all primitive target language
        // types (which we need to do in order to support fake meta objects)
        if (element != StackElement::PrimitiveTypeEntry
            && element != StackElement::FunctionTypeEntry) {
            TypeEntryPtr tmp = m_context->db->findType(name);
            if (tmp && !tmp->isNamespace())
                qCWarning(lcShiboken).noquote().nospace()
                    << "Duplicate type entry: '" << name << '\'';
        }

        if (element == StackElement::EnumTypeEntry) {
            const auto enumIdentifiedByIndex =
                indexOfAttribute(attributes, enumIdentifiedByValueAttribute);
            const QString identifiedByValue = enumIdentifiedByIndex != -1
                ? attributes.takeAt(enumIdentifiedByIndex).value().toString() : QString();
            if (name.isEmpty()) {
                name = identifiedByValue;
            } else if (!identifiedByValue.isEmpty()) {
                m_error = u"can't specify both 'name' and 'identified-by-value' attributes"_s;
                return false;
            }
        }

        if (name.isEmpty()) {
            m_error = u"no 'name' attribute specified"_s;
            return false;
        }

        switch (element) {
        case StackElement::CustomTypeEntry:
            top->entry = parseCustomTypeEntry(reader, name, versionRange.since, &attributes);
            if (Q_UNLIKELY(!top->entry))
                return false;
            break;
        case StackElement::PrimitiveTypeEntry:
            top->entry = parsePrimitiveTypeEntry(reader, name, versionRange.since, &attributes);
            if (Q_UNLIKELY(!top->entry))
                return false;
            break;
        case StackElement::ContainerTypeEntry:
            top->entry = parseContainerTypeEntry(reader, name, versionRange.since, &attributes);
            if (top->entry == nullptr)
                return false;
            break;

        case StackElement::SmartPointerTypeEntry:
            top->entry = parseSmartPointerEntry(reader, name, versionRange.since, &attributes);
            if (top->entry == nullptr)
                return false;
            break;
        case StackElement::EnumTypeEntry:
            m_currentEnum = parseEnumTypeEntry(reader, name, versionRange.since, &attributes);
            if (Q_UNLIKELY(!m_currentEnum))
                return false;
            top->entry = m_currentEnum;
            break;

        case StackElement::ValueTypeEntry:
           top->entry = parseValueTypeEntry(reader, name, versionRange.since, &attributes);
           if (top->entry == nullptr)
               return false;
           break;
        case StackElement::NamespaceTypeEntry:
            top->entry = parseNamespaceTypeEntry(reader, name, versionRange.since, &attributes);
            if (top->entry == nullptr)
                return false;
            break;
        case StackElement::ObjectTypeEntry:
        case StackElement::InterfaceTypeEntry: {
            if (!checkRootElement())
                return false;
            auto ce = std::make_shared<ObjectTypeEntry>(name, versionRange.since, currentParentTypeEntry());
            top->entry = ce;
            if (!applyComplexTypeAttributes(reader, ce, &attributes))
                return false;
        }
            break;
        case StackElement::FunctionTypeEntry:
            top->entry = parseFunctionTypeEntry(reader, name, versionRange.since, &attributes);
            if (Q_UNLIKELY(!top->entry))
                return false;
            break;
        case StackElement::TypedefTypeEntry:
            top->entry = parseTypedefEntry(reader, name, topElement,
                                           versionRange.since, &attributes);
            if (top->entry == nullptr)
                return false;
            break;
        default:
            Q_ASSERT(false);
        }

        if (top->entry) {
            if (checkDuplicatedTypeEntry(reader, element, top->entry->name())
                && !m_context->db->addType(top->entry, &m_error)) {
                return false;
            }
        } else {
            qCWarning(lcShiboken).noquote().nospace()
                << u"Type: "_s + name + u" was rejected by typesystem"_s;
        }

    } else if (element == StackElement::InjectDocumentation) {
        if (!parseInjectDocumentation(reader, topElement, &attributes))
            return false;
    } else if (element == StackElement::ModifyDocumentation) {
        if (!parseModifyDocumentation(reader, topElement, &attributes))
            return false;
    } else if (element != StackElement::None) {
        bool topLevel = element == StackElement::Root
                        || element == StackElement::SuppressedWarning
                        || element == StackElement::Rejection
                        || element == StackElement::LoadTypesystem
                        || element == StackElement::InjectCode
                        || element == StackElement::ExtraIncludes
                        || element == StackElement::SystemInclude
                        || element == StackElement::ConversionRule
                        || element == StackElement::AddFunction
                        || element == StackElement::DeclareFunction
                        || element == StackElement::Template
                        || element == StackElement::OpaqueContainer
                        || element == StackElement::OverloadRemoval;

        if (!topLevel && m_stack.at(m_stack.size() - 2) == StackElement::Root) {
            m_error = u"Tag requires parent: '"_s + tagName.toString() + u'\'';
            return false;
        }

        switch (element) {
        case StackElement::Root:
            top->entry = parseRootElement(reader, versionRange.since, &attributes);
            if (!top->entry)
                return false;
            break;
        case StackElement::LoadTypesystem:
            if (!loadTypesystem(reader, &attributes))
                return false;
            break;
        case StackElement::RejectEnumValue:
            if (!parseRejectEnumValue(reader, &attributes))
                return false;
            break;
        case StackElement::ReplaceType:
            if (!parseReplaceArgumentType(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::ConversionRule:
            if (!TypeSystemParser::parseCustomConversion(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::NativeToTarget:
            if (!parseNativeToTarget(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::TargetToNative: {
            if (topElement != StackElement::ConversionRule) {
                m_error = u"Target to Native conversions can only be specified for custom conversion rules."_s;
                return false;
            }

            const auto topParent = m_stack.value(m_stack.size() - 3, StackElement::None);
            if (isTypeEntry(topParent)) {
                const auto replaceIndex = indexOfAttribute(attributes, replaceAttribute);
                const bool replace = replaceIndex == -1
                    || convertBoolean(attributes.takeAt(replaceIndex).value(),
                                      replaceAttribute, true);
                auto customConversion = CustomConversion::getCustomConversion(top->entry);
                if (!customConversion) {
                    m_error = msgMissingCustomConversion(top->entry);
                    return false;
                }
                customConversion->setReplaceOriginalTargetToNativeConversions(replace);
            }
        }
        break;
        case StackElement::AddConversion:
            if (!parseAddConversion(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::ModifyArgument:
            if (!parseModifyArgument(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::NoNullPointers:
            if (!parseNoNullPointer(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::DefineOwnership:
            if (!parseDefineOwnership(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::SuppressedWarning: {
            const auto textIndex = indexOfAttribute(attributes, textAttribute);
            if (textIndex == -1) {
                qCWarning(lcShiboken) << "Suppressed warning with no text specified";
            } else {
                const QString suppressedWarning =
                    attributes.takeAt(textIndex).value().toString();
                if (!m_context->db->addSuppressedWarning(suppressedWarning,
                                                         m_generate == TypeEntry::GenerateCode,
                                                         &m_error)) {
                    return false;
                }
            }
        }
            break;
        case StackElement::Rename:
             if (!parseRename(reader, topElement, &attributes))
                 return false;
             break;
        case StackElement::RemoveArgument:
            if (topElement != StackElement::ModifyArgument) {
                m_error = u"Removing argument requires argument modification as parent"_s;
                return false;
            }

            top->functionMods.last().argument_mods().last().setRemoved(true);
            break;

        case StackElement::ModifyField:
            if (!parseModifyField(reader, &attributes))
                return false;
            break;
        case StackElement::DeclareFunction:
        case StackElement::AddFunction:
            if (!parseAddFunction(reader, topElement, element, &attributes))
                return false;
            break;
        case StackElement::AddPyMethodDef:
            if (!parseAddPyMethodDef(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::Property:
            if (!parseProperty(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::ModifyFunction:
            if (!parseModifyFunction(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::ReplaceDefaultExpression:
            if (!parseReplaceDefaultExpression(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::RemoveDefaultExpression:
            top->functionMods.last().argument_mods().last().setRemovedDefaultExpression(true);
            break;
        case StackElement::ReferenceCount:
            if (!parseReferenceCount(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::ParentOwner:
            if (!parseParentOwner(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::Array:
            if (topElement != StackElement::ModifyArgument) {
                m_error = u"array must be child of modify-argument"_s;
                return false;
            }
            top->functionMods.last().argument_mods().last().setArray(true);
            break;
        case StackElement::InjectCode:
            if (!parseInjectCode(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::Include:
            if (!parseInclude(reader, topElement, top->entry, &attributes))
                return false;
            break;
        case StackElement::Rejection:
            if (!addRejection(m_context->db, m_generate == TypeEntry::GenerateCode,
                              &attributes, &m_error)) {
                return false;
            }
            break;
        case StackElement::SystemInclude:
            if (!parseSystemInclude(reader, &attributes))
                return false;
            break;
        case StackElement::Template:
            m_templateEntry = parseTemplate(&attributes);
            if (m_templateEntry == nullptr)
                return false;
            break;
        case StackElement::InsertTemplate:
            m_templateInstance = parseInsertTemplate(reader, topElement, &attributes);
            if (!m_templateInstance.has_value())
                return false;
            break;
        case StackElement::Replace:
            if (!parseReplace(reader, topElement, &attributes))
                return false;
            break;
        case StackElement::OpaqueContainer:
            if (!parseOpaqueContainerElement(&attributes))
        case StackElement::Configuration:
            if (!parseConfiguration(topElement, &attributes))
                return false;
            break;
        case StackElement::OverloadRemoval:
            if (!parseOverloadRemoval(topElement, &attributes))
                return false;
            break;
        default:
            break; // nada
        }
    }

    if (!attributes.isEmpty()) {
        const QString message = msgUnusedAttributes(tagName, attributes);
        qCWarning(lcShiboken, "%s", qPrintable(msgReaderWarning(reader, message)));
    }

    return true;
}
