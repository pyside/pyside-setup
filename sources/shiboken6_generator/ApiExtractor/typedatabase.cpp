// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "typedatabase.h"
#include "abstractmetatype.h"
#include "addedfunction.h"
#include "messages.h"
#include "typesystemparser_p.h"
#include "clangparser/compilersupport.h"
#include "complextypeentry.h"
#include "constantvaluetypeentry.h"
#include "containertypeentry.h"
#include "customtypenentry.h"
#include "debughelpers_p.h"
#include "exception.h"
#include "flagstypeentry.h"
#include "functiontypeentry.h"
#include "namespacetypeentry.h"
#include "objecttypeentry.h"
#include "primitivetypeentry.h"
#include "optionsparser.h"
#include "pythontypeentry.h"
#include "smartpointertypeentry.h"
#include "typedefentry.h"
#include "typesystemtypeentry.h"
#include "varargstypeentry.h"
#include "voidtypeentry.h"
#include "conditionalstreamreader.h"
#include "predefined_templates.h"
#include "modifications.h"


#include <QtCore/qbuffer.h>
#include <QtCore/qfile.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qlist.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qversionnumber.h>
#include <QtCore/qxmlstream.h>
#include "reporthandler.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

using TypeDatabaseParserContextPtr = std::shared_ptr<TypeDatabaseParserContext>;

// package -> api-version

static QString wildcardToRegExp(QString w)
{
    w.replace(u'?', u'.');
    w.replace(u'*', ".*"_L1);
    return w;
}

using ApiVersion = std::pair<QRegularExpression, QVersionNumber>;
using ApiVersions = QList<ApiVersion>;

Q_GLOBAL_STATIC(ApiVersions, apiVersions)

struct PythonType
{
    QString name;
    QString checkFunction;
    TypeSystem::CPythonType type;
};

using PythonTypes = QList<PythonType>;

static const PythonTypes &builtinPythonTypes()
{
    static const PythonTypes result{
        // "Traditional" custom types
        // numpy
        {u"PyArrayObject"_s, u"PyArray_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyBuffer"_s, u"Shiboken::Buffer::checkType"_s, TypeSystem::CPythonType::Other},
        {u"PyByteArray"_s, u"PyByteArray_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyBytes"_s, u"PyBytes_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyCallable"_s, u"PyCallable_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyDate"_s, u"PyDate_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyDateTime"_s, u"PyDateTime_Check_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyDict"_s, u"PyDict_Check"_s, TypeSystem::CPythonType::Other},
        // Convenience macro in sbkconverter.h
        {u"PyObject"_s, u"true"_s, TypeSystem::CPythonType::Other},
        // shiboken-specific
        {u"PyPathLike"_s, u"Shiboken::String::checkPath"_s, TypeSystem::CPythonType::Other},
        {u"PySequence"_s, u"Shiboken::String::checkIterableArgument"_s,
         TypeSystem::CPythonType::Other},
        {u"PyUnicode"_s, u"PyUnicode_Check"_s, TypeSystem::CPythonType::String},
        {u"PyTypeObject"_s, u"PyType_Check"_s, TypeSystem::CPythonType::Other},
        {u"str"_s, u"Shiboken::String::check"_s, TypeSystem::CPythonType::String},
        // Types used as target lang API types for primitive types
        {u"PyBool"_s, u"PyBool_Check"_s, TypeSystem::CPythonType::Bool},
        {u"PyComplex"_s, u"PyComplex_Check"_s, TypeSystem::CPythonType::Other},
        {u"PyLong"_s, u"PyLong_Check"_s, TypeSystem::CPythonType::Integer},
        {u"PyFloat"_s, u"PyFloat_Check"_s, TypeSystem::CPythonType::Float},
        // Single character strings to match C++ char types
        {u"SbkChar"_s, u"SbkChar_Check"_s, TypeSystem::CPythonType::String}
    };
    return result;
}

struct SuppressedWarning
{
    QRegularExpression pattern;
    QString rawText;
    bool generate; // Current type system
    mutable bool matched = false;
};

QList<OptionDescription> TypeDatabase::options()
{
    return {
        {u"api-version=<\"package mask\">,<\"version\">"_s,
         u"Specify the supported api version used to generate the bindings"_s},
        {u"drop-type-entries=\"<TypeEntry0>[;TypeEntry1;...]\""_s,
         u"Semicolon separated list of type system entries (classes, namespaces,\n"
         "global functions and enums) to be dropped from generation."_s},
        {u"-T<path>"_s, {} },
        {u"typesystem-paths="_s + OptionsParser::pathSyntax(),
         u"Paths used when searching for typesystems"_s},
        {u"force-process-system-include-paths="_s + OptionsParser::pathSyntax(),
         u"Include paths that are considered as system headers by the C++ parser, but should still "
         "be processed to extract types (e.g. Qt include paths in a yocto sysroot)"_s},
        {u"keywords=keyword1[,keyword2,...]"_s,
         u"A comma-separated list of keywords for conditional typesystem parsing"_s},
        {u"module-uses-gil=<yes|no|auto>"_s,
         u"Whether a module uses the GIL in a free-threaded build (default: yes;\n"
         "auto: not used when compiled with Py_GIL_DISABLED);\n"
         "the module-uses-gil attribute of the typesystem element overrides it"_s},
    };
}

struct TypeDatabaseOptions
{
    QStringList m_dropTypeEntries;
    QStringList m_forceProcessSystemIncludes;
    QStringList m_typesystemKeywords;
    QStringList m_typesystemPaths;
    bool m_suppressWarnings = true;
    TypeSystem::ModuleGil m_moduleGil = TypeSystem::ModuleGil::Used;
};

class TypeDatabaseOptionsParser : public OptionsParser
{
public:
    explicit TypeDatabaseOptionsParser(TypeDatabaseOptions *o) : m_options(o) {}

    bool handleBoolOption(const QString &key, OptionSource source) override;
    bool handleOption(const QString &key, const QString &value, OptionSource source) override;

private:
    TypeDatabaseOptions *m_options;
};

bool TypeDatabaseOptionsParser::handleBoolOption(const QString &key, OptionSource source)
{
    switch (source) {
    case OptionSource::CommandLine:
    case OptionSource::ProjectFile:
        if (key == u"no-suppress-warnings") {
            m_options->m_suppressWarnings = false;
            return true;
        }
        break;
    case OptionSource::CommandLineSingleDash:
        if (key.startsWith(u'T')) { // "-T/path" ends up a bool option
            m_options->m_typesystemPaths += key.sliced(1).split(QDir::listSeparator(),
                                                                Qt::SkipEmptyParts);
            return true;
        }
        break;
    }
    return false;
}

bool TypeDatabaseOptionsParser::handleOption(const QString &key, const QString &value,
                                             OptionSource source)
{
    if (source == OptionSource::CommandLineSingleDash)
        return false;
    if (key == u"api-version") {
        const auto fullVersions = QStringView{value}.split(u'|');
        for (const auto &fullVersion : fullVersions) {
            const auto parts = fullVersion.split(u',');
            const QString package = parts.size() == 1
                                    ? u"*"_s : parts.constFirst().toString();
            const QString version = parts.constLast().toString();
            if (!TypeDatabase::setApiVersion(package, version))
                throw Exception(msgInvalidVersion(package, version));
        }
        return true;
    }

    if (key == u"drop-type-entries") {
        m_options->m_dropTypeEntries = value.split(u';');
        m_options->m_dropTypeEntries.sort();
        return true;
    }

    if (key == u"keywords") {
        m_options->m_typesystemKeywords = value.split(u',');
        return true;
    }

    if (key == u"module-uses-gil") {
        if (value == u"yes" || value == u"true")
            m_options->m_moduleGil = TypeSystem::ModuleGil::Used;
        else if (value == u"no" || value == u"false")
            m_options->m_moduleGil = TypeSystem::ModuleGil::NotUsed;
        else if (value == u"auto")
            m_options->m_moduleGil = TypeSystem::ModuleGil::Auto;
        else
            throw Exception(u"Invalid value \""_s + value + u"\" of module-uses-gil."_s);
        return true;
    }

    if (key == u"typesystem-paths") {
        m_options->m_typesystemPaths += value.split(QDir::listSeparator(),
                                                    Qt::SkipEmptyParts);
        return true;
    }

    if (key == u"force-process-system-include-paths") {
        m_options->m_forceProcessSystemIncludes += value.split(QDir::listSeparator(),
                                                               Qt::SkipEmptyParts);
        return true;
    }

    if (source == OptionSource::ProjectFile) {
        if (key == u"typesystem-path") {
            m_options->m_typesystemPaths += value;
            return true;
        }
    }

    return false;
}

struct TypeDatabasePrivate : public TypeDatabaseOptions
{
    TypeSystemTypeEntryCPtr defaultTypeSystemType() const;
    TypeEntryPtr findType(const QString &name) const;
    TypeEntryCList findCppTypes(const QString &name) const;
    bool addType(TypeEntryPtr e, QString *errorMessage = nullptr);
    bool parseFile(QIODevice *device, TypeDatabase *db, bool generate = true);
    static bool parseFile(const TypeDatabaseParserContextPtr &context,
                          QIODevice *device, bool generate = true);
    bool parseFile(const TypeDatabaseParserContextPtr &context,
                   const QString &filename, const QString &currentPath, bool generate);
    bool prepareParsing(QFile &file, const QString &origFileName,
                        const QString &currentPath = {});

    QString modifiedTypesystemFilepath(const QString& tsFile,
                                       const QString &currentPath) const;
    void addBuiltInType(const TypeEntryPtr &e);
    PrimitiveTypeEntryPtr addBuiltInPrimitiveType(const QString &name,
                                                const TypeSystemTypeEntryCPtr &root,
                                                const QString &rootPackage,
                                                const CustomTypeEntryPtr &targetLang);
    void addBuiltInCppStringPrimitiveType(const QString &name,
                                          const QString &viewName,
                                          const TypeSystemTypeEntryCPtr &root,
                                          const QString &rootPackage,
                                          const CustomTypeEntryPtr &targetLang);
    void addBuiltInPrimitiveTypes();
    void addBuiltInContainerTypes(const TypeDatabaseParserContextPtr &context);
    bool addOpaqueContainers(const TypeDatabaseParserContextPtr &context);
    TypeEntryMultiMapConstIteratorRange findTypeRange(const QString &name) const;
    template <class Predicate>
    TypeEntryCList findTypesHelper(const QString &name, Predicate pred) const;
    template <class Type, class Predicate>
    QList<std::shared_ptr<const Type> > findTypesByTypeHelper(Predicate pred) const;
    TypeEntryPtr resolveTypeDefEntry(const TypedefEntryPtr &typedefEntry, QString *errorMessage);
    template <class String>
    bool isSuppressedWarningHelper(const String &s) const;
    bool resolveSmartPointerInstantiations(const TypeDatabaseParserContextPtr &context) const;
    bool resolveSmartPointerInstantiations(const SmartPointerTypeEntryPtr &smartPointerEntry,
                                           const QString &s) const;
    bool resolveSmartPointerExclusions(const SmartPointerTypeEntryPtr &smartPointerEntry,
                                       const QString &s) const;
    void formatDebug(QDebug &d) const;
    void formatBuiltinTypes(QDebug &d) const;

    TypeEntryMultiMap m_entries; // Contains duplicate entries (cf addInlineNamespaceLookups).
    QMap<QString, FlagsTypeEntryPtr> m_flagsEntries;
    TypedefEntryMap m_typedefEntries;
    TemplateEntryMap m_templates;
    QList<SuppressedWarning> m_suppressedWarnings;
    QList<TypeSystemTypeEntryCPtr > m_typeSystemEntries; // maintain order, default is first.

    AddedFunctionList m_globalUserFunctions;
    FunctionModificationList m_functionMods;
    OverloadRemovalRules m_overloadRemovalRules;

    QStringList m_requiredTargetImports;

    QHash<QString, bool> m_parsedTypesystemFiles;

    QList<TypeRejection> m_rejections;
};

static const char ENV_TYPESYSTEMPATH[] = "TYPESYSTEMPATH";

TypeDatabase::TypeDatabase() : d(new TypeDatabasePrivate)
{
    // Environment TYPESYSTEMPATH
    if (qEnvironmentVariableIsSet(ENV_TYPESYSTEMPATH)) {
        d->m_typesystemPaths
            += qEnvironmentVariable(ENV_TYPESYSTEMPATH).split(QDir::listSeparator(),
                                                              Qt::SkipEmptyParts);
    }

    d->addBuiltInType(TypeEntryPtr(new VoidTypeEntry()));
    d->addBuiltInType(TypeEntryPtr(new VarargsTypeEntry()));
    for (const auto &pt : builtinPythonTypes())
        d->addBuiltInType(TypeEntryPtr(new PythonTypeEntry(pt.name, pt.checkFunction, pt.type)));

    for (const auto &p : predefinedTemplates())
        addTemplate(p.name, p.content);
}

TypeDatabase::~TypeDatabase()
{
    delete d;
}

std::shared_ptr<OptionsParser> TypeDatabase::createOptionsParser()
{
    return std::make_shared<TypeDatabaseOptionsParser>(d);
}

TypeDatabase *TypeDatabase::instance(bool newInstance)
{
    static TypeDatabase *db = nullptr;
    if (!db || newInstance) {
        delete db;
        db = new TypeDatabase;
    }
    return db;
}

// A list of regex/replacements to fix int types like "ushort" to "unsigned short"
// unless present in TypeDatabase
struct IntTypeNormalizationEntry
{
    QRegularExpression regex;
    QString replacement;
};

using IntTypeNormalizationEntries = QList<IntTypeNormalizationEntry>;

static const IntTypeNormalizationEntries &intTypeNormalizationEntries()
{
    static IntTypeNormalizationEntries result;
    static bool firstTime = true;
    if (firstTime) {
        firstTime = false;
        for (const auto &intType : {"char"_L1, "short"_L1, "int"_L1, "long"_L1}) {
            if (!TypeDatabase::instance()->findType(u'u' + intType)) {
                IntTypeNormalizationEntry entry;
                entry.replacement = "unsigned "_L1 + intType;
                entry.regex.setPattern("\\bu"_L1 + intType + "\\b"_L1);
                Q_ASSERT(entry.regex.isValid());
                result.append(entry);
            }
        }
    }
    return result;
}

// Normalization helpers
enum CharCategory : std::uint8_t { Space, Identifier, Other };

static CharCategory charCategory(QChar c)
{
    if (c.isSpace())
        return Space;
    if (c.isLetterOrNumber() || c == u'_')
        return Identifier;
    return Other;
}

// Normalize a C++ function signature:
// Drop space except between identifiers ("unsigned int", "const int")
static QString normalizeCppFunctionSignature(const QString &signatureIn)
{
    const QString signature = signatureIn.simplified();
    QString result;
    result.reserve(signature.size());

    CharCategory lastNonSpaceCategory = Other;
    bool pendingSpace = false;
    for (QChar c : signature) {
        if (c.isSpace()) {
            pendingSpace = true;
        } else {
            const auto category = charCategory(c);
            if (pendingSpace) {
                if (lastNonSpaceCategory == Identifier && category == Identifier)
                    result.append(u' ');
                pendingSpace = false;
            }
            lastNonSpaceCategory = category;
            result.append(c);
        }
    }
    return result;
}

// Normalize a signature for <add-function> by removing spaces
QString TypeDatabase::normalizedAddedFunctionSignature(const QString &signature)
{
    return normalizeCppFunctionSignature(signature);
}

// Normalize a signature for matching by <modify-function>/<function>
// by removing spaces and changing const-ref to value.
// FIXME: PYSIDE7: Check whether the above simple normalization can be used
// here as well. Note though that const-ref would then have to be spelled out
// in typeystem XML.
QString TypeDatabase::normalizedSignature(const QString &signature)
{
    // QMetaObject::normalizedSignature() changes const-ref to value and
    // changes "unsigned int" to "uint" which is undone by the below code
    QByteArray normalizedB = QMetaObject::normalizedSignature(signature.toUtf8().constData());
    QString normalized = QLatin1StringView(normalizedB);

    if (instance() && signature.contains(u"unsigned")) {
        const IntTypeNormalizationEntries &entries = intTypeNormalizationEntries();
        for (const auto &entry : entries)
            normalized.replace(entry.regex, entry.replacement);
    }

    return normalized;
}

QStringList TypeDatabase::requiredTargetImports() const
{
    return d->m_requiredTargetImports;
}

void TypeDatabase::addRequiredTargetImport(const QString& moduleName)
{
    if (!d->m_requiredTargetImports.contains(moduleName))
        d->m_requiredTargetImports << moduleName;
}

static QStringList platformKeywords()
{
    static constexpr auto unixKeyword = "unix"_L1;
    static constexpr auto linuxKeyword = "linux"_L1;
    switch (clang::optionsTriplet().platform()) {
    case Platform::Unix:
        return {unixKeyword};
    case Platform::Linux:
        return {unixKeyword, linuxKeyword};
    case Platform::Windows:
        return {"windows"_L1};
    case Platform::macOS:
        return {unixKeyword, "darwin"_L1};
    case Platform::Android:
        return {unixKeyword, linuxKeyword, "android"_L1};
    case Platform::iOS:
        return {unixKeyword, "ios"_L1};
    case Platform::Unknown:
        break;
    }
    return {};
}

TypeSystem::ModuleGil TypeDatabase::moduleGil() const
{
    return d->m_moduleGil;
}

QStringList TypeDatabase::typesystemKeywords() const
{
    QStringList result = d->m_typesystemKeywords + platformKeywords();

    for (const auto &d : std::as_const(d->m_dropTypeEntries))
        result.append("no_"_L1 + d);

    switch (clang::emulatedCompilerLanguageLevel()) {
    case LanguageLevel::Cpp11:
        result.append(u"c++11"_s);
        break;
    case LanguageLevel::Cpp14:
        result.append(u"c++14"_s);
        break;
    case LanguageLevel::Cpp17:
        result.append(u"c++17"_s);
        break;
    case LanguageLevel::Cpp20:
        result.append(u"c++20"_s);
        break;
    default:
        break;
    }
    return result;
}

IncludeList TypeDatabase::extraIncludes(const QString& className) const
{
    auto typeEntry = findComplexType(className);
    return typeEntry ? typeEntry->extraIncludes() :  IncludeList();
}

const QStringList &TypeDatabase::forceProcessSystemIncludes() const
{
    return d->m_forceProcessSystemIncludes;
}

void TypeDatabase::addForceProcessSystemInclude(const QString &name)
{
    d->m_forceProcessSystemIncludes.append(name);
}

// Add a lookup for the short name excluding inline namespaces
// so that "std::shared_ptr" finds "std::__1::shared_ptr" as well.
// Note: This inserts duplicate TypeEntryPtr into m_entries.
void TypeDatabase::addInlineNamespaceLookups(const NamespaceTypeEntryCPtr &n)
{
    TypeEntryList additionalEntries; // Store before modifying the hash
    for (const auto &entry : std::as_const(d->m_entries)) {
        if (entry->isChildOf(n))
            additionalEntries.append(entry);
    }
    for (const auto &ae : std::as_const(additionalEntries))
        d->m_entries.insert(ae->shortName(), ae);
}

ContainerTypeEntryPtr TypeDatabase::findContainerType(const QString &name) const
{
    QString template_name = name;

    const auto pos = name.indexOf(u'<');
    if (pos > 0)
        template_name = name.left(pos);

    auto type_entry = findType(template_name);
    if (type_entry && type_entry->isContainer())
        return std::static_pointer_cast<ContainerTypeEntry>(type_entry);
    return {};
}

static bool inline useType(const TypeEntryCPtr &t)
{
    return !t->isPrimitive()
        || std::static_pointer_cast<const PrimitiveTypeEntry>(t)->preferredTargetLangType();
}

FunctionTypeEntryPtr TypeDatabase::findFunctionType(const QString &name) const
{
    const auto entries = d->findTypeRange(name);
    for (const TypeEntryPtr &entry : entries) {
        if (entry->type() == TypeEntry::FunctionType && useType(entry))
            return std::static_pointer_cast<FunctionTypeEntry>(entry);
    }
    return {};
}

void TypeDatabase::addTypeSystemType(const TypeSystemTypeEntryCPtr &e)
{
    d->m_typeSystemEntries.append(e);
}

TypeSystemTypeEntryCPtr TypeDatabase::findTypeSystemType(const QString &name) const
{
    for (auto entry : d->m_typeSystemEntries) {
        if (entry->name() == name)
            return entry;
    }
    return {};
}

TypeSystemTypeEntryCPtr TypeDatabase::defaultTypeSystemType() const
{
    return d->defaultTypeSystemType();
}

QString TypeDatabase::loadedTypeSystemNames() const
{
    QString result;
    for (const auto &entry : d->m_typeSystemEntries) {
        if (!result.isEmpty())
            result += u", "_s;
        result += entry->name();
    }
    return result;
}

QList<TypeSystemTypeEntryCPtr> TypeDatabase::typeSystemEntries() const
{
    return d->m_typeSystemEntries;
}

TypeSystemTypeEntryCPtr TypeDatabasePrivate::defaultTypeSystemType() const
{
    return m_typeSystemEntries.value(0, nullptr);
}

QString TypeDatabase::defaultPackageName() const
{
    Q_ASSERT(!d->m_typeSystemEntries.isEmpty());
    return d->m_typeSystemEntries.constFirst()->name();
}

TypeEntryPtr TypeDatabase::findType(const QString& name) const
{
    return d->findType(name);
}

TypeEntryPtr TypeDatabasePrivate::findType(const QString& name) const
{
    const auto entries = findTypeRange(name);
    for (const auto &entry : entries) {
        if (useType(entry))
            return entry;
    }
    return {};
}

template <class Predicate>
TypeEntryCList TypeDatabasePrivate::findTypesHelper(const QString &name, Predicate pred) const
{
    TypeEntryCList result;
    const auto entries = findTypeRange(name);
    for (const auto &entry : entries) {
        if (pred(entry))
            result.append(entry);
    }
    return result;
}

template<class Type, class Predicate>
QList<std::shared_ptr<const Type> > TypeDatabasePrivate::findTypesByTypeHelper(Predicate pred) const
{
    QList<std::shared_ptr<const Type> > result;
    for (const auto &entry : m_entries) {
        if (pred(entry))
            result.append(std::static_pointer_cast<const Type>(entry));
    }
    return result;
}

TypeEntryCList TypeDatabase::findTypes(const QString &name) const
{
    return d->findTypesHelper(name, useType);
}

static bool useCppType(const TypeEntryCPtr &t)
{
    bool result = false;
    switch (t->type()) {
    case TypeEntry::PrimitiveType:
    case TypeEntry::VoidType:
    case TypeEntry::FlagsType:
    case TypeEntry::EnumType:
    case TypeEntry::TemplateArgumentType:
    case TypeEntry::BasicValueType:
    case TypeEntry::ContainerType:
    case TypeEntry::ObjectType:
    case TypeEntry::ArrayType:
    case TypeEntry::CustomType:
    case TypeEntry::SmartPointerType:
    case TypeEntry::TypedefType:
    case TypeEntry::PythonType:
    case TypeEntry::VarargsType:
    case TypeEntry::EnumValue:
    case TypeEntry::ConstantValueType:
        result = useType(t);
        break;
    default:
        break;
    }
    return result;
}

TypeEntryCList TypeDatabase::findCppTypes(const QString &name) const
{
    return d->findCppTypes(name);
}

TypeEntryCList TypeDatabasePrivate::findCppTypes(const QString &name) const
{
    return findTypesHelper(name, useCppType);
}

const TypeEntryMultiMap &TypeDatabase::entries() const
{
    return d->m_entries;
}

const TypedefEntryMap &TypeDatabase::typedefEntries() const
{
    return d->m_typedefEntries;
}

TypeEntryMultiMapConstIteratorRange TypeDatabasePrivate::findTypeRange(const QString &name) const
{
    const auto range = m_entries.equal_range(name);
    return {range.first, range.second};
}

PrimitiveTypeEntryCList TypeDatabase::primitiveTypes() const
{
    auto pred = [](const TypeEntryCPtr &t) { return t->isPrimitive(); };
    return d->findTypesByTypeHelper<PrimitiveTypeEntry>(pred);
}

ContainerTypeEntryCList TypeDatabase::containerTypes() const
{
    auto pred = [](const TypeEntryCPtr &t) { return t->isContainer(); };
    return d->findTypesByTypeHelper<ContainerTypeEntry>(pred);
}

SmartPointerTypeEntryList TypeDatabase::smartPointerTypes() const
{
    auto pred = [](const TypeEntryCPtr &t) { return t->isSmartPointer(); };
    return d->findTypesByTypeHelper<SmartPointerTypeEntry>(pred);
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const TypeRejection &r)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "TypeRejection(type=" << r.matchType << ", class="
        << r.className.pattern() << ", pattern=" << r.pattern.pattern() << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

void TypeDatabase::addRejection(const TypeRejection &r)
{
    d->m_rejections << r;
}

// Match class name only
bool TypeDatabase::isClassRejected(const QString& className, QString *reason) const
{
    for (const TypeRejection& r : d->m_rejections) {
        if (r.matchType == TypeRejection::ExcludeClass && r.className.match(className).hasMatch()) {
            r.matched = true;
            if (reason)
                *reason = msgRejectReason(r);
            return true;
        }
    }
    return false;
}

// Match class name and function/enum/field
static bool findRejection(const QList<TypeRejection> &rejections,
                          TypeRejection::MatchType matchType,
                          const QString& className, const QString& name,
                          QString *reason = nullptr)
{
    Q_ASSERT(matchType != TypeRejection::ExcludeClass);
    for (const TypeRejection& r : rejections) {
        if (r.matchType == matchType && r.pattern.match(name).hasMatch()
            && r.className.match(className).hasMatch()) {
            r.matched = true;
            if (reason)
                *reason = msgRejectReason(r, name);
            return true;
        }
    }
    return false;
}

bool TypeDatabase::isEnumRejected(const QString& className, const QString& enumName, QString *reason) const
{
    return findRejection(d->m_rejections, TypeRejection::Enum, className, enumName, reason);
}

TypeEntryPtr TypeDatabasePrivate::resolveTypeDefEntry(const TypedefEntryPtr &typedefEntry,
                                             QString *errorMessage)
{
    QString sourceName = typedefEntry->sourceType();
    const auto lessThanPos = sourceName.indexOf(u'<');
    if (lessThanPos != -1)
        sourceName.truncate(lessThanPos);
    ComplexTypeEntryPtr source;
    for (const auto &e : findTypeRange(sourceName)) {
        switch (e->type()) {
        case TypeEntry::BasicValueType:
        case TypeEntry::ContainerType:
        case TypeEntry::ObjectType:
        case TypeEntry::SmartPointerType:
            source = std::dynamic_pointer_cast<ComplexTypeEntry>(e);
            Q_ASSERT(source);
            break;
        default:
            break;
        }
    }
    if (!source) {
        if (errorMessage)
            *errorMessage = msgUnableToResolveTypedef(typedefEntry->sourceType(), sourceName);
        return nullptr;
    }

    m_typedefEntries.insert(typedefEntry->qualifiedCppName(), typedefEntry);
    return TypeDatabase::initializeTypeDefEntry(typedefEntry, source);
}

ComplexTypeEntryPtr
    TypeDatabase::initializeTypeDefEntry(const TypedefEntryPtr &typedefEntry,
                                         const ComplexTypeEntryCPtr &source)
{
    ComplexTypeEntryPtr result(static_cast<ComplexTypeEntry *>(source->clone()));
    result->useAsTypedef(typedefEntry);
    typedefEntry->setSource(source);
    typedefEntry->setTarget(result);
    return result;
}

bool TypeDatabase::addType(const TypeEntryPtr &e, QString *errorMessage)
{
    return d->addType(e, errorMessage);
}

bool TypeDatabasePrivate::addType(TypeEntryPtr e, QString *errorMessage)
{
    if (e->type() == TypeEntry::TypedefType) {
        e = resolveTypeDefEntry(std::static_pointer_cast<TypedefEntry>(e), errorMessage);
        if (Q_UNLIKELY(!e))
            return false;
    }
    m_entries.insert(e->qualifiedCppName(), e);
    return true;
}

// Add a dummy value entry for non-type template parameters
ConstantValueTypeEntryPtr
    TypeDatabase::addConstantValueTypeEntry(const QString &value,
                                            const TypeEntryCPtr &parent)
{
    auto result = std::make_shared<ConstantValueTypeEntry>(value, parent);
    result->setCodeGeneration(TypeEntry::GenerateNothing);
    addType(result);
    return result;
}

bool TypeDatabase::isFunctionRejected(const QString& className, const QString& functionName,
                                      QString *reason) const
{
    return findRejection(d->m_rejections, TypeRejection::Function, className, functionName, reason);
}

bool TypeDatabase::isFieldRejected(const QString& className, const QString& fieldName,
                                   QString *reason) const
{
    return findRejection(d->m_rejections, TypeRejection::Field, className, fieldName, reason);
}

bool TypeDatabase::isArgumentTypeRejected(const QString& className, const QString& typeName,
                                          QString *reason) const
{
    return findRejection(d->m_rejections, TypeRejection::ArgumentType, className, typeName, reason);
}

bool TypeDatabase::isReturnTypeRejected(const QString& className, const QString& typeName,
                                        QString *reason) const
{
    return findRejection(d->m_rejections, TypeRejection::ReturnType, className, typeName, reason);
}

static bool matchesUnqualifiedFlagName(const QString &candidate, const QString &name)
{
    return candidate.size() > name.size()
        && candidate.endsWith(name)
        && candidate.at(candidate.size() - name.size() -1) == u':';
}

FlagsTypeEntryPtr TypeDatabase::findFlagsType(const QString &name) const
{
    FlagsTypeEntryPtr result;
    const auto end = d->m_flagsEntries.cend();
    auto it = d->m_flagsEntries.constFind(name);
    if (it != end) {
        result = it.value();
    } else {
        // Last hope, search for flag without scope  inside of flags hash.
        // This is mostly triggered by parseQ_Properties() for unqualified flags.
        int matchCount = 0;
        for (it = d->m_flagsEntries.cbegin(); it != end; ++it) {
            if (matchesUnqualifiedFlagName(it.key(), name)) {
                result = it.value();
                if (++matchCount > 1) { // ambiguous, like "Flags"
                    result.reset();
                    break;
                }
            }
        }
    }
    return result;
}

void TypeDatabase::addFlagsType(const FlagsTypeEntryPtr &fte)
{
    d->m_flagsEntries[fte->originalName()] = fte;
}

TemplateEntryPtr TypeDatabase::findTemplate(const QString &name) const
{
    return d->m_templates[name];
}

void TypeDatabase::addTemplate(const TemplateEntryPtr &t)
{
    d->m_templates[t->name()] = t;
}

void TypeDatabase::addTemplate(const QString &name, const QString &code)
{
    auto te = std::make_shared<TemplateEntry>();
    te->setName(name);
    te->addCode(code);
    addTemplate(te);
}

AddedFunctionList TypeDatabase::globalUserFunctions() const
{
    return d->m_globalUserFunctions;
}

void TypeDatabase::addGlobalUserFunctions(const AddedFunctionList &functions)
{
    d->m_globalUserFunctions << functions;
}

AddedFunctionList TypeDatabase::findGlobalUserFunctions(const QString& name) const
{
    AddedFunctionList addedFunctions;
    for (const AddedFunctionPtr &func : d->m_globalUserFunctions) {
        if (func->name() == name)
            addedFunctions.append(func);
    }
    return addedFunctions;
}

void TypeDatabase::addGlobalUserFunctionModifications(const FunctionModificationList &functionModifications)
{
    d->m_functionMods << functionModifications;
}

QString TypeDatabase::globalNamespaceClassName(const TypeEntryCPtr  & /*entry*/)
{
    return u"Global"_s;
}

FunctionModificationList
    TypeDatabase::globalFunctionModifications(const QStringList &signatures) const
{
    FunctionModificationList lst;
    for (const auto &mod : d->m_functionMods) {
        if (mod.matches(signatures))
            lst << mod;
    }

    return lst;
}

const OverloadRemovalRules &TypeDatabase::overloadRemovalRules() const
{
    return d->m_overloadRemovalRules;
}

void TypeDatabase::addOverloadRemovalRule(const OverloadRemovalRule &r)
{
    d->m_overloadRemovalRules.append(r);
}

bool TypeDatabase::addSuppressedWarning(const QString &warning, bool generate,
                                        QString *errorMessage)
{
    QString pattern;
    if (warning.startsWith(u'^') && warning.endsWith(u'$')) {
        pattern = warning;
    } else {
        // Legacy syntax: Use wildcards '*' (unless escaped by '\')
        QList<qsizetype> asteriskPositions;
        const auto warningSize = warning.size();
        for (qsizetype i = 0, warningSize = warning.size(); i < warningSize; ++i) {
            if (warning.at(i) == u'\\')
                ++i;
            else if (warning.at(i) == u'*')
                asteriskPositions.append(i);
        }
        asteriskPositions.append(warningSize);

        pattern.append(u'^');
        qsizetype lastPos = 0;
        for (qsizetype a = 0, aSize = asteriskPositions.size(); a < aSize; ++a) {
            if (a)
                pattern.append(".*"_L1);
            const auto nextPos = asteriskPositions.at(a);
            if (nextPos > lastPos)
                pattern.append(QRegularExpression::escape(warning.mid(lastPos, nextPos - lastPos)));
            lastPos = nextPos + 1;
        }
        pattern.append(u'$');
    }

    QRegularExpression expression(pattern);
    if (!expression.isValid()) {
        *errorMessage = u"Invalid message pattern \""_s + warning
            + u"\": "_s + expression.errorString();
        return false;
    }
    expression.setPatternOptions(expression.patternOptions() | QRegularExpression::MultilineOption);

    d->m_suppressedWarnings.append({expression, warning, generate});
    return true;
}

bool TypeDatabase::isSuppressedWarning(QStringView s) const
{
    if (!d->m_suppressWarnings)
        return false;
    auto wit = std::find_if(d->m_suppressedWarnings.cbegin(), d->m_suppressedWarnings.cend(),
                            [&s] (const SuppressedWarning &e) {
                                return e.pattern.matchView(s).hasMatch();
                            });
    const bool found = wit != d->m_suppressedWarnings.cend();
    if (found)
        wit->matched = true;
    return found;
}

QString TypeDatabase::modifiedTypesystemFilepath(const QString& tsFile, const QString &currentPath) const
{
    return d->modifiedTypesystemFilepath(tsFile, currentPath);
}

void TypeDatabase::logUnmatched() const
{
    for (auto &sw : d->m_suppressedWarnings) {
        if (sw.generate && !sw.matched)
            qWarning("Unmatched suppressed warning: \"%s\"", qPrintable(sw.rawText));
    }

    for (auto &tr : d->m_rejections) {
        if (tr.generate && !tr.matched) {
            QDebug d = qWarning();
            d.noquote();
            d.nospace();
            d << "Unmatched rejection: " << tr.matchType;
            if (!tr.className.pattern().isEmpty())
                d << " class " << tr.className.pattern();
            if (!tr.pattern.pattern().isEmpty())
                d << " \"" << tr.pattern.pattern() << '"';
        }
    }
}

QString TypeDatabasePrivate::modifiedTypesystemFilepath(const QString& tsFile,
                                                        const QString &currentPath) const
{
    const QFileInfo tsFi(tsFile);
    if (tsFi.isAbsolute()) // No point in further lookups
        return tsFi.absoluteFilePath();
    if (tsFi.isFile()) // Make path absolute
        return tsFi.absoluteFilePath();
    if (!currentPath.isEmpty()) {
        const QFileInfo fi(currentPath + u'/' + tsFile);
        if (fi.isFile())
            return fi.absoluteFilePath();
    }
    for (const QString &path : m_typesystemPaths) {
        const QFileInfo fi(path + u'/' + tsFile);
        if (fi.isFile())
            return fi.absoluteFilePath();
    }
    return tsFile;
}

void TypeDatabasePrivate::addBuiltInContainerTypes(const TypeDatabaseParserContextPtr &context)
{
    // Unless the user has added the standard containers (potentially with
    // some opaque types), add them by default.
    const bool hasStdArray = findType(u"std::array"_s) != nullptr;
    const bool hasStdPair = findType(u"std::pair"_s) != nullptr;
    const bool hasStdList = findType(u"std::list"_s) != nullptr;
    const bool hasStdVector = findType(u"std::vector"_s) != nullptr;
    const bool hasStdMap = findType(u"std::map"_s) != nullptr;
    const bool hasStdUnorderedMap = findType(u"std::unordered_map"_s) != nullptr;
    const bool hasStdSpan = findType(u"std::span"_s) != nullptr;

    if (hasStdPair && hasStdList && hasStdVector && hasStdMap && hasStdUnorderedMap)
        return;

    QByteArray ts = R"(<?xml version="1.0" encoding="UTF-8"?><typesystem>)";
    if (!hasStdArray) {
        ts += containerTypeSystemSnippet(
            "std::array", "list", "array",
            "shiboken_conversion_cppsequence_to_pylist",
            "PySequence",
            "shiboken_conversion_pyiterable_to_cpparray");
    }
    if (!hasStdPair) {
        ts += containerTypeSystemSnippet(
                  "std::pair", "pair", "utility",
                  "shiboken_conversion_cpppair_to_pytuple",
                  "PySequence", "shiboken_conversion_pysequence_to_cpppair");
    }
    if (!hasStdList) {
        ts += containerTypeSystemSnippet(
                  "std::list", "list", "list",
                  "shiboken_conversion_cppsequence_to_pylist",
                  "PySequence",
                  "shiboken_conversion_pyiterable_to_cppsequentialcontainer");
    }
    if (!hasStdVector) {
        ts += containerTypeSystemSnippet(
                  "std::vector", "list", "vector",
                  "shiboken_conversion_cppsequence_to_pylist",
                  "PySequence",
                  "shiboken_conversion_pyiterable_to_cppsequentialcontainer_reserve");
    }
    if (!hasStdMap) {
        ts += containerTypeSystemSnippet(
                  "std::map", "map", "map",
                  "shiboken_conversion_stdmap_to_pydict",
                  "PyDict", "shiboken_conversion_pydict_to_stdmap");
    }
    if (!hasStdUnorderedMap) {
        ts += containerTypeSystemSnippet(
                  "std::unordered_map", "map", "unordered_map",
                  "shiboken_conversion_stdmap_to_pydict",
                  "PyDict", "shiboken_conversion_pydict_to_stdmap");
    }
    if (!hasStdSpan
        && clang::emulatedCompilerLanguageLevel() >= LanguageLevel::Cpp20) {
        auto spanSnip = containerTypeSystemSnippet(
                            "std::span", "span", "span",
                            "shiboken_conversion_cppsequence_to_pylist");
        auto pos = spanSnip.indexOf('>');
        spanSnip.insert(pos, R"( view-on="std::vector")");
        ts += spanSnip;
    }

    ts += "</typesystem>";
    QBuffer buffer(&ts);
    buffer.open(QIODevice::ReadOnly);
    const bool ok = parseFile(context, &buffer, true);
    Q_ASSERT(ok);
}

bool TypeDatabasePrivate::addOpaqueContainers(const TypeDatabaseParserContextPtr &context)
{
    const auto &och = context->opaqueContainerHash;
    for (auto it = och.cbegin(), end = och.cend(); it != end; ++it) {
        const QString &name = it.key();
        auto te = findType(name);
        if (!te || !te->isContainer()) {
            qCWarning(lcShiboken, "No container \"%s\" found.", qPrintable(name));
            return false;
        }
        auto cte = std::static_pointer_cast<ContainerTypeEntry>(te);
        cte->appendOpaqueContainers(it.value());
    }
    return true;
}

bool TypeDatabase::parseFile(const QString &filename, bool generate)
{
    QString filepath = modifiedTypesystemFilepath(filename, {});
    QFile file(filepath);
    return d->prepareParsing(file, filename) && d->parseFile(&file, this, generate);
}

bool TypeDatabase::parseFile(const TypeDatabaseParserContextPtr &context,
                             const QString &filename, const QString &currentPath,
                             bool generate)
{
    return d->parseFile(context, filename, currentPath, generate);
}

bool TypeDatabasePrivate::prepareParsing(QFile &file, const QString &origFileName,
                                         const QString &currentPath)
{
    const QString &filepath = file.fileName();
    if (!file.exists()) {
        m_parsedTypesystemFiles[filepath] = false;
        QString message = u"Can't find "_s + origFileName;
        if (!currentPath.isEmpty())
            message += u", current path: "_s + currentPath;
        message += u", typesystem paths: "_s + m_typesystemPaths.join(u", "_s);
        qCWarning(lcShiboken, "%s", qPrintable(message));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_parsedTypesystemFiles[filepath] = false;
        qCWarning(lcShiboken, "%s", qPrintable(msgCannotOpenForReading(file)));
        return false;
    }

    m_parsedTypesystemFiles[filepath] = true;
    return true;
}

bool TypeDatabasePrivate::parseFile(const TypeDatabaseParserContextPtr &context,
                                    const QString &filename, const QString &currentPath,
                                    bool generate)
{
    // Prevent recursion when including self.
    QString filepath = modifiedTypesystemFilepath(filename, currentPath);
    const auto it = m_parsedTypesystemFiles.constFind(filepath);
    if (it != m_parsedTypesystemFiles.cend())
        return it.value();

    QFile file(filepath);
    if (!prepareParsing(file, filename, currentPath))
        return false;

    const bool ok = parseFile(context, &file, generate);
    m_parsedTypesystemFiles[filepath] = ok;
    return ok;
}

bool TypeDatabase::parseFile(QIODevice *device, bool generate)
{
    return d->parseFile(device, this, generate);
}

bool TypeDatabasePrivate::parseFile(QIODevice *device, TypeDatabase *db, bool generate)
{
    const auto context = std::make_shared<TypeDatabaseParserContext>();
    context->db = db;

    if (!parseFile(context, device, generate))
        return false;

    addBuiltInPrimitiveTypes();
    addBuiltInContainerTypes(context);
    return addOpaqueContainers(context)
        && resolveSmartPointerInstantiations(context);
}

bool TypeDatabase::parseFile(const TypeDatabaseParserContextPtr &context,
                             QIODevice *device, bool generate)
{
    return TypeDatabasePrivate::parseFile(context, device, generate);
}

bool TypeDatabasePrivate::parseFile(const TypeDatabaseParserContextPtr &context,
                                    QIODevice *device, bool generate)
{
    ConditionalStreamReader reader(device);
    const auto typesystemKeywords = context->db->typesystemKeywords();
    if (generate) {
        ReportHandler::addGeneralMessage("Type System keywords: "_L1
                                         + typesystemKeywords.join(", "_L1) + u'\n');
    }
    reader.setConditions(typesystemKeywords);
    TypeSystemParser handler(context, generate);
    const bool result = handler.parse(reader);
    if (!result) {
        qCWarning(lcShiboken, "%s", qPrintable(handler.errorString()));
        return false;
    }
    return result;
}

// Split a type list potentially with template types
// "A<B,C>,D" -> ("A<B,C>", "D")
static QStringList splitTypeList(const QString &s)
{
    QStringList result;
    int templateDepth = 0;
    qsizetype lastPos = 0;
    const auto size = s.size();
    for (qsizetype i = 0; i < size; ++i) {
        switch (s.at(i).toLatin1()) {
        case '<':
            ++templateDepth;
            break;
        case '>':
            --templateDepth;
            break;
        case ',':
            if (templateDepth == 0) {
                result.append(s.mid(lastPos, i - lastPos).trimmed());
                lastPos = i + 1;
            }
            break;
        default:
            break;
        }
    }
    if (lastPos < size)
        result.append(s.mid(lastPos, size - lastPos).trimmed());
    return result;
}

bool TypeDatabasePrivate::resolveSmartPointerInstantiations(
    const SmartPointerTypeEntryPtr &smartPointerEntry, const QString &s) const
{
    const auto instantiationNames = splitTypeList(s);
    SmartPointerTypeEntry::Instantiations instantiations;
    instantiations.reserve(instantiationNames.size());
    for (const auto &instantiation : instantiationNames) {
        QString name;
        QString type = instantiation;
        const auto equalsPos = instantiation.indexOf(u'=');
        if (equalsPos != -1) {
            type.truncate(equalsPos);
            name = instantiation.mid(equalsPos + 1);
        }

        const auto typeEntries = findCppTypes(type);
        if (typeEntries.isEmpty()) {
            const QString m = msgCannotFindTypeEntryForSmartPointer(type,
                                                                    smartPointerEntry->name());
            qCWarning(lcShiboken, "%s", qPrintable(m));
            return false;
        }
        if (typeEntries.size() > 1) {
            const QString m = msgAmbiguousTypesFound(type, typeEntries);
            qCWarning(lcShiboken, "%s", qPrintable(m));
            return false;
        }
        instantiations.append({name, typeEntries.constFirst()});
    }
    smartPointerEntry->setInstantiations(instantiations);
    return true;
}

bool TypeDatabasePrivate::resolveSmartPointerExclusions(
    const SmartPointerTypeEntryPtr &smartPointerEntry, const QString &s) const
{
    const auto excludedNames = splitTypeList(s);
    TypeEntryCList excluded;

    excluded.reserve(excludedNames.size());
    for (const auto &excludedName : excludedNames) {
        const auto typeEntries = findCppTypes(excludedName);
        if (typeEntries.isEmpty()) {
            const QString m = msgCannotFindTypeEntryForSmartPointer(excludedName,
                                                                    smartPointerEntry->name());
            qCWarning(lcShiboken, "%s", qPrintable(m));
            return false;
        }
        if (typeEntries.size() > 1) {
            const QString m = msgAmbiguousTypesFound(excludedName, typeEntries);
            qCWarning(lcShiboken, "%s", qPrintable(m));
            return false;
        }
        excluded.append(typeEntries.constFirst());
    }
    smartPointerEntry->setExcludedInstantiations(excluded);
    return true;
}

bool TypeDatabasePrivate::resolveSmartPointerInstantiations(const TypeDatabaseParserContextPtr &context) const
{
    const auto &instantiations = context->smartPointerInstantiations;
    for (auto it = instantiations.cbegin(), end = instantiations.cend(); it != end; ++it) {
        const auto &entry = it.value();
        if ((!entry.instantiations.isEmpty()
             && !resolveSmartPointerInstantiations(it.key(), entry.instantiations))
            || (!entry.excludedInstantiations.isEmpty()
                 && !resolveSmartPointerExclusions(it.key(), entry.excludedInstantiations))) {
            return false;
        }
    }
    return true;
}

PrimitiveTypeEntryPtr TypeDatabase::findPrimitiveType(const QString& name) const
{
    const auto entries = d->findTypeRange(name);
    for (const auto &entry : entries) {
        if (entry->isPrimitive()) {
            auto pe = std::static_pointer_cast<PrimitiveTypeEntry>(entry);
            if (pe->preferredTargetLangType())
                return pe;
        }
    }

    return nullptr;
}

ComplexTypeEntryPtr TypeDatabase::findComplexType(const QString& name) const
{
    const auto entries = d->findTypeRange(name);
    for (const auto &entry : entries) {
        if (entry->isComplex() && useType(entry))
            return std::static_pointer_cast<ComplexTypeEntry>(entry);
    }
    return nullptr;
}

ObjectTypeEntryPtr TypeDatabase::findObjectType(const QString& name) const
{
    const auto entries = d->findTypeRange(name);
    for (const auto &entry : entries) {
        if (entry && entry->isObject() && useType(entry))
            return std::static_pointer_cast<ObjectTypeEntry>(entry);
    }
    return nullptr;
}

NamespaceTypeEntryList TypeDatabase::findNamespaceTypes(const QString& name) const
{
    NamespaceTypeEntryList result;
    const auto entries = d->findTypeRange(name);
    for (const auto &entry : entries) {
        if (entry->isNamespace())
            result.append(std::static_pointer_cast<NamespaceTypeEntry>(entry));
    }
    return result;
}

NamespaceTypeEntryPtr TypeDatabase::findNamespaceType(const QString& name,
                                                    const QString &fileName) const
{
    const auto entries = findNamespaceTypes(name);
    // Preferably check on matching file name first, if a pattern was given.
    if (!fileName.isEmpty()) {
        for (const auto &entry : entries) {
            if (entry->hasPattern() && entry->matchesFile(fileName))
                return entry;
        }
    }
    for (const auto &entry : entries) {
        if (!entry->hasPattern())
            return entry;
    }
    return nullptr;
}

bool TypeDatabase::shouldDropTypeEntry(const QString& fullTypeName) const
{
    return d->m_dropTypeEntries.contains(fullTypeName);
}

void TypeDatabase::setDropTypeEntries(const QStringList &dropTypeEntries)
{
    d->m_dropTypeEntries = dropTypeEntries;
    d->m_dropTypeEntries.sort();
}

static bool computeTypeIndexes = true;
static int maxTypeIndex;

static bool typeEntryLessThan(const TypeEntryCPtr &t1, const TypeEntryCPtr &t2)
{
    if (t1->revision() < t2->revision())
        return true;
    return t1->revision() == t2->revision()
        && t1->qualifiedCppName() < t2->qualifiedCppName();
}

static void _computeTypeIndexes()
{
    auto *tdb = TypeDatabase::instance();

    TypeEntryList list;

    // Group type entries by revision numbers
    const auto &allEntries = tdb->entries();
    list.reserve(allEntries.size());
    for (auto  tit = allEntries.cbegin(), end = allEntries.cend(); tit != end; ++tit) {
        const TypeEntryPtr &entry = tit.value();
        if (entry->isPrimitive()
            || entry->isContainer()
            || entry->isFunction()
            || !entry->generateCode()
            || entry->isEnumValue()
            || entry->isVarargs()
            || entry->isTypeSystem()
            || entry->isVoid()
            || entry->isCustom())
            continue;
        if (!list.contains(entry)) // Remove duplicates
            list.append(entry);
    }

    // Sort the type entries by revision, name
    std::sort(list.begin(), list.end(), typeEntryLessThan);

    maxTypeIndex = 0;
    for (const TypeEntryPtr &e : std::as_const(list))
        e->setSbkIndex(maxTypeIndex++);
    computeTypeIndexes = false;
}

void TypeEntry::setRevision(int r)
{
    if (setRevisionHelper(r))
        computeTypeIndexes = true;
}

int TypeEntry::sbkIndex() const
{
    if (computeTypeIndexes)
        _computeTypeIndexes();
    return sbkIndexHelper();
}

int getMaxTypeIndex()
{
    if (computeTypeIndexes)
        _computeTypeIndexes();
    return maxTypeIndex;
}

void TypeDatabase::clearApiVersions()
{
    apiVersions()->clear();
}

bool TypeDatabase::setApiVersion(const QString& packageWildcardPattern, const QString &version)
{
    const QString packagePattern = wildcardToRegExp(packageWildcardPattern.trimmed());
    const QVersionNumber versionNumber = QVersionNumber::fromString(version);
    if (versionNumber.isNull())
        return false;
    ApiVersions &versions = *apiVersions();
    for (auto &version : versions) {
        if (version.first.pattern() == packagePattern) {
            version.second = versionNumber;
            return true;
        }
    }
    const QRegularExpression packageRegex(packagePattern);
    if (!packageRegex.isValid())
        return false;
    versions.append(std::make_pair(packageRegex, versionNumber));
    return true;
}

bool TypeDatabase::checkApiVersion(const QString &package,
                                   const VersionRange &vr)
{
    const ApiVersions &versions = *apiVersions();
    if (versions.isEmpty()) // Nothing specified: use latest.
        return true;
    for (const auto &version : versions) {
        if (version.first.match(package).hasMatch())
            return version.second >= vr.since
                && version.second <= vr.until;
    }
    return false;
}

bool TypeDatabase::hasDroppedTypeEntries() const
{
    return !d->m_dropTypeEntries.isEmpty();
}

#ifndef QT_NO_DEBUG_STREAM
void TypeDatabase::formatDebug(QDebug &debug) const
{
    d->formatDebug(debug);
}

void TypeDatabasePrivate::formatDebug(QDebug &d) const
{
    d << "TypeDatabase("
      << "entries[" << m_entries.size() << "]=";
    for (auto it = m_entries.cbegin(), end = m_entries.cend(); it != end; ++it)
        d << "  " << it.value() << '\n';
    if (!m_typedefEntries.isEmpty()) {
        d << "typedefs[" << m_typedefEntries.size() << "]=(";
        const auto begin = m_typedefEntries.cbegin();
        for (auto it = begin, end = m_typedefEntries.cend(); it != end; ++it) {
            if (it != begin)
                d << ", ";
            d << "  " << it.value() << '\n';
        }
        d << ")\n";
    }
    if (!m_templates.isEmpty()) {
        d << "templates[" << m_templates.size() << "]=(";
        const auto begin = m_templates.cbegin();
        for (auto  it = begin, end = m_templates.cend(); it != end; ++it) {
            if (it != begin)
                d << ", ";
            d << it.value();
        }
        d << ")\n";
    }
    if (!m_flagsEntries.isEmpty()) {
        d << "flags[" << m_flagsEntries.size() << "]=(";
        const auto begin = m_flagsEntries.cbegin();
        for (auto it = begin, end = m_flagsEntries.cend(); it != end; ++it) {
            if (it != begin)
                d << ", ";
            d << it.value();
        }
        d << ")\n";
    }
    d <<"\nglobalUserFunctions=" << m_globalUserFunctions << '\n';
    formatList(d, "globalFunctionMods", m_functionMods, "\n");
    d << ')';
}

// Helpers for dumping out primitive type info

struct formatPrimitiveEntry
{
    explicit formatPrimitiveEntry(const PrimitiveTypeEntryCPtr &e) : m_pe(e) {}

    PrimitiveTypeEntryCPtr m_pe;
};

QDebug operator<<(QDebug debug, const formatPrimitiveEntry &fe)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    const QString &name = fe.m_pe->name();
    const QString &targetLangName = fe.m_pe->targetLangApiName();
    debug << '"' << name << '"';
    if (name != targetLangName)
        debug << " (\"" << targetLangName << "\")";
    if (fe.m_pe->isBuiltIn())
        debug << " [builtin]";
    if (isExtendedCppPrimitive(fe.m_pe)) {
        debug << " [";
        if (!isCppPrimitive(fe.m_pe))
            debug << "extended ";
        debug << "C++]";
    }
    return debug;
}

// Sort primitive types for displaying; base type and typedef'ed types
struct PrimitiveFormatListEntry
{
    PrimitiveTypeEntryCPtr baseType;
    PrimitiveTypeEntryCList typedefs;
};

static bool operator<(const PrimitiveFormatListEntry &e1, const PrimitiveFormatListEntry &e2)
{
    return e1.baseType->name() < e2.baseType->name();
}

using PrimitiveFormatListEntries = QList<PrimitiveFormatListEntry>;

static qsizetype indexOf(const PrimitiveFormatListEntries &e, const PrimitiveTypeEntryCPtr &needle)
{
    for (qsizetype i = 0, size = e.size(); i < size; ++i) {
        if (e.at(i).baseType == needle)
            return i;
    }
    return -1;
}

void TypeDatabase::formatBuiltinTypes(QDebug debug) const
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();

    // Determine base types and their typedef'ed types
    QList<PrimitiveFormatListEntry> primitiveEntries;
    for (const auto &e : std::as_const(d->m_entries)) {
        if (e->isPrimitive()) {
            auto pe = std::static_pointer_cast<const PrimitiveTypeEntry>(e);
            auto basic = basicReferencedTypeEntry(pe);
            if (basic != pe) {
                const auto idx = indexOf(primitiveEntries, basic);
                if (idx != -1)
                    primitiveEntries[idx].typedefs.append(pe);
                else
                    primitiveEntries.append(PrimitiveFormatListEntry{basic, {pe}});
            } else {
                primitiveEntries.append(PrimitiveFormatListEntry{pe, {}});
            }
        }
    }

    std::sort(primitiveEntries.begin(), primitiveEntries.end());

    for (const auto &e : std::as_const(primitiveEntries)) {
        debug << "Primitive: " << formatPrimitiveEntry(e.baseType) << '\n';
        for (const auto &pe : e.typedefs)
            debug << "             "  << formatPrimitiveEntry(pe) << '\n';
    }
}

void TypeDatabasePrivate::addBuiltInType(const TypeEntryPtr &e)
{
    e->setBuiltIn(true);
    addType(e);
}

PrimitiveTypeEntryPtr
    TypeDatabasePrivate::addBuiltInPrimitiveType(const QString &name,
                                          const TypeSystemTypeEntryCPtr &root,
                                          const QString &rootPackage,
                                          const CustomTypeEntryPtr &targetLang)
{
    auto result = std::make_shared<PrimitiveTypeEntry>(name, QVersionNumber{}, root);
    result->setTargetLangApiType(targetLang);
    result->setTargetLangPackage(rootPackage);
    addBuiltInType(result);
    return result;
}

void TypeDatabasePrivate::addBuiltInCppStringPrimitiveType(const QString &name,
                                                    const QString &viewName,
                                                    const TypeSystemTypeEntryCPtr &root,
                                                    const QString &rootPackage,
                                                    const CustomTypeEntryPtr &targetLang)

{
    auto stringType = addBuiltInPrimitiveType(name, root, rootPackage,
                                              targetLang);
    auto viewType = addBuiltInPrimitiveType(viewName, root, rootPackage,
                                            nullptr);
    viewType->setViewOn(stringType);
}

void TypeDatabasePrivate::addBuiltInPrimitiveTypes()
{
    auto root = defaultTypeSystemType();
    const QString &rootPackage = root->name();

    // C++ primitive types
    auto pyLongEntry = findType(u"PyLong"_s);
    Q_ASSERT(pyLongEntry && pyLongEntry->isCustom());
    auto pyLongCustomEntry = std::static_pointer_cast<CustomTypeEntry>(pyLongEntry);
    auto pyBoolEntry = findType(u"PyBool"_s);
    Q_ASSERT(pyBoolEntry && pyBoolEntry->isCustom());
    auto sbkCharEntry = findType(u"SbkChar"_s);
    Q_ASSERT(sbkCharEntry && sbkCharEntry->isCustom());
    auto sbkCharCustomEntry = std::static_pointer_cast<CustomTypeEntry>(sbkCharEntry);

    auto pyBoolCustomEntry = std::static_pointer_cast<CustomTypeEntry>(pyBoolEntry);
    for (const auto &t : AbstractMetaType::cppIntegralTypes()) {
        if (!m_entries.contains(t)) {
            CustomTypeEntryPtr targetLangApi = pyLongCustomEntry;
            if (t == u"bool")
                targetLangApi = pyBoolCustomEntry;
            else if (AbstractMetaType::cppCharTypes().contains(t))
                targetLangApi = sbkCharCustomEntry;
            addBuiltInPrimitiveType(t, root, rootPackage, targetLangApi);
        }
    }

    auto pyFloatEntry = findType(u"PyFloat"_s);
    Q_ASSERT(pyFloatEntry && pyFloatEntry->isCustom());
    auto pyFloatCustomEntry = std::static_pointer_cast<CustomTypeEntry>(pyFloatEntry);
    for (const auto &t : AbstractMetaType::cppFloatTypes()) {
        if (!m_entries.contains(t))
            addBuiltInPrimitiveType(t, root, rootPackage, pyFloatCustomEntry);
    }

    auto pyUnicodeEntry = findType(u"PyUnicode"_s);
    Q_ASSERT(pyUnicodeEntry && pyUnicodeEntry->isCustom());
    auto pyUnicodeCustomEntry = std::static_pointer_cast<CustomTypeEntry>(pyUnicodeEntry);

    constexpr auto stdString = "std::string"_L1;
    if (!m_entries.contains(stdString)) {
        addBuiltInCppStringPrimitiveType(stdString, u"std::string_view"_s,
                                         root, rootPackage,
                                         pyUnicodeCustomEntry);
    }
    constexpr auto stdWString = "std::wstring"_L1;
    if (!m_entries.contains(stdWString)) {
        addBuiltInCppStringPrimitiveType(stdWString, u"std::wstring_view"_s,
                                         root, rootPackage,
                                         pyUnicodeCustomEntry);
    }

    // Prevent rejection of operator<=>() due to mismatched return type.
    if (clang::emulatedCompilerLanguageLevel() >= LanguageLevel::Cpp20) {
        for (const QString &ordering : {u"std::strong_ordering"_s, u"std::partial_ordering"_s}) {
            if (!m_entries.contains(ordering)) {
                    auto entry = std::make_shared<CustomTypeEntry>(ordering, QVersionNumber{}, root);
                    entry->setTargetLangPackage(rootPackage);
                    m_entries.insert(ordering, entry);
            }
        }
    }
}

QDebug operator<<(QDebug d, const TypeDatabase &db)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    db.formatDebug(d);
    return d;
}
#endif // !QT_NO_DEBUG_STREAM
