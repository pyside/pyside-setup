// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "qtdocgenerator.h"
#include "generatorcontext.h"
#include "codesnip.h"
#include "exception.h"
#include "abstractmetaargument.h"
#include "apiextractorresult.h"
#include "qtxmltosphinx.h"
#include "rstformat.h"
#include "ctypenames.h"
#include "pytypenames.h"
#include <abstractmetaenum.h>
#include <abstractmetafield.h>
#include <abstractmetafunction.h>
#include <abstractmetalang.h>
#include "abstractmetalang_helpers.h"
#include <fileout.h>
#include <messages.h>
#include <modifications.h>
#include <propertyspec.h>
#include <reporthandler.h>
#include <textstream.h>
#include <typedatabase.h>
#include <functiontypeentry.h>
#include <enumtypeentry.h>
#include <complextypeentry.h>
#include <flagstypeentry.h>
#include <primitivetypeentry.h>
#include <smartpointertypeentry.h>
#include <typesystemtypeentry.h>
#include <qtdocparser.h>
#include <doxygenparser.h>
#include <xmlutils.h>


#include <QtCore/qtextstream.h>
#include <QtCore/qfile.h>
#include <QtCore/qdir.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qoperatingsystemversion.h>
#include <QtCore/qset.h>

#include <algorithm>
#include <limits>
#include <unordered_set>

using namespace Qt::StringLiterals;

static inline QString classScope(const AbstractMetaClassCPtr &metaClass)
{
    return metaClass->fullName();
}

struct DocClassEntry
{
    QString name;
    QString fullName;
    QString file;
    bool nested = false;
};

static bool classEntryLessThan(const DocClassEntry &e1, const DocClassEntry &e2)
{
    return e1.name < e2.name;
}

struct DocPackage
{
    QList<DocClassEntry> classPages;
    QStringList decoratorPages;
    AbstractMetaFunctionCList globalFunctions;
    AbstractMetaEnumList globalEnums;
};

struct DocGeneratorOptions
{
    QtXmlToSphinxParameters parameters;
    QString extraSectionDir;
    QString additionalDocumentationList;
    QString inheritanceFile;
    bool doxygen = false;
    bool inheritanceDiagram = true;
};

struct GeneratorDocumentation
{
    struct Property
    {
        QString name;
        Documentation documentation;
        AbstractMetaType type;
        AbstractMetaFunctionCPtr getter;
        AbstractMetaFunctionCPtr setter;
        AbstractMetaFunctionCPtr reset;
        AbstractMetaFunctionCPtr notify;
    };

    AbstractMetaFunctionCList allFunctions;
    AbstractMetaFunctionCList tocNormalFunctions; // Index lists
    AbstractMetaFunctionCList tocVirtuals;
    AbstractMetaFunctionCList tocSignalFunctions;
    AbstractMetaFunctionCList tocSlotFunctions;
    AbstractMetaFunctionCList tocClassFunctions;
    AbstractMetaFunctionCList tocStaticFunctions;

    QList<Property> properties;
};

static bool operator<(const GeneratorDocumentation::Property &lhs,
                      const GeneratorDocumentation::Property &rhs)
{
    return lhs.name < rhs.name;
}

struct ResolvedDocImage
{
    QString name;
    QString absoluteSourceFilePath;
    QString targetDir;
};

QDebug operator<<(QDebug debug, const ResolvedDocImage &i)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "ResolvedDocImage(\"" << i.name << "\", \"" << i.absoluteSourceFilePath
          << "\" -> \"" << i.targetDir << "\")";
    return debug;
}

static QString propertyRefTarget(const QString &name)
{
    QString result = name;
    // For sphinx referencing, disambiguate the target from the getter name
    // by appending an invisible "Hangul choseong filler" character.
    result.append(QChar(0x115F));
    return result;
}

constexpr auto additionalDocumentationOption = "additional-documentation"_L1;

constexpr auto none = "None"_L1;

static bool shouldSkip(const AbstractMetaFunctionCPtr &func)
{
    if (DocParser::skipForDocumentation(func))
        return true;

    // Search a const clone (QImage::bits() vs QImage::bits() const)
    if (func->isConstant())
        return false;

    const AbstractMetaArgumentList funcArgs = func->arguments();
    const auto &ownerFunctions = func->ownerClass()->functions();
    for (const auto &f : ownerFunctions) {
        if (f != func
            && f->isConstant()
            && f->name() == func->name()
            && f->arguments().size() == funcArgs.size()) {
            // Compare each argument
            bool cloneFound = true;

            const AbstractMetaArgumentList fargs = f->arguments();
            for (qsizetype i = 0, max = funcArgs.size(); i < max; ++i) {
                if (funcArgs.at(i).type().typeEntry() != fargs.at(i).type().typeEntry()) {
                    cloneFound = false;
                    break;
                }
            }
            if (cloneFound)
                return true;
        }
    }
    return false;
}

static bool functionSort(const AbstractMetaFunctionCPtr &func1, const AbstractMetaFunctionCPtr &func2)
{
    const bool ctor1 = func1->isConstructor();
    if (ctor1 != func2->isConstructor())
        return ctor1;
    const QString &name1 = func1->name();
    const QString &name2 = func2->name();
    if (name1 != name2)
        return name1 < name2;
    return func1->arguments().size() < func2->arguments().size();
}

static inline QVersionNumber versionOf(const TypeEntryCPtr &te)
{
    if (te) {
        const auto version = te->version();
        if (!version.isNull() && version > QVersionNumber(0, 0))
            return version;
    }
    return {};
}

struct docRef
{
    explicit docRef(const char *kind, QAnyStringView name) :
        m_kind(kind), m_name(name) {}

    const char *m_kind;
    QAnyStringView m_name;
};

static TextStream &operator<<(TextStream &s, const docRef &dr)
{
    s << ':' << dr.m_kind << ":`" << dr.m_name << '`';
    return s;
}

static QString fileNameToTocEntry(const QString &fileName)
{
    constexpr auto rstSuffix = ".rst"_L1;

    QString result = fileName;
    if (result.endsWith(rstSuffix))
        result.chop(rstSuffix.size()); // Remove the .rst extension
    // skip namespace if necessary
    auto lastDot = result.lastIndexOf(u'.');
    if (lastDot != -1)
        result.remove(0, lastDot + 1);
    return result;
}

static void readExtraDoc(const QFileInfo &fi,
                         const QString &moduleName,
                         const QString &outputDir,
                         DocPackage *docPackage, QStringList *extraTocEntries)
{
    // Strip to "Property.rst" in output directory
    const QString newFileName = fi.fileName().sliced(moduleName.size() + 1);
    const QString fullClassName = fi.completeBaseName().sliced(moduleName.size() + 1);
    QFile sourceFile(fi.absoluteFilePath());
    if (!sourceFile.open(QIODevice::ReadOnly|QIODevice::Text)) {
        qCWarning(lcShibokenDoc, "%s", qPrintable(msgCannotOpenForReading(sourceFile)));
        return;
    }
    const QByteArray contents = sourceFile.readAll();
    sourceFile.close();
    QFile targetFile(outputDir + u'/' + newFileName);
    if (!targetFile.open(QIODevice::WriteOnly|QIODevice::Text)) {
        qCWarning(lcShibokenDoc, "%s", qPrintable(msgCannotOpenForWriting(targetFile)));
        return;
    }
    targetFile.write(contents);
    if (contents.contains("decorator::")) {
        docPackage->decoratorPages.append(newFileName);
    } else {
        QString name = fullClassName;
        auto dot = name.lastIndexOf(u'.');
        if (dot != -1)
            name.remove(0, dot + 1);
        docPackage->classPages.append({name, fullClassName, newFileName, false});
    }
    extraTocEntries->append(fileNameToTocEntry(newFileName));
}

// Format a short documentation reference (automatically dropping the prefix
// by using '~'), usable for property/attributes ("attr").
struct shortDocRef
{
    explicit shortDocRef(const char *kind, QAnyStringView name) :
        m_kind(kind), m_name(name) {}

    const char *m_kind;
    QAnyStringView m_name;
};

static TextStream &operator<<(TextStream &s, const shortDocRef &sdr)
{
    s << ':' << sdr.m_kind << ":`~" << sdr.m_name << '`';
    return s;
}

struct functionRef : public docRef
{
    explicit functionRef(QAnyStringView name) : docRef("meth", name) {}
};

struct classRef : public shortDocRef
{
    explicit classRef(QAnyStringView name) : shortDocRef("class", name) {}
};

struct propRef : public shortDocRef // Attribute/property (short) reference
{
    explicit propRef(const QString &target) :
        shortDocRef("attr", target) {}
};

struct headline
{
    explicit headline(QAnyStringView title, char underLineChar = '-') :
        m_title(title), m_underLineChar(underLineChar) {}

    QAnyStringView m_title;
    char m_underLineChar;
};

static TextStream &operator<<(TextStream &s, const headline &h)
{
    s << h.m_title << '\n' << Pad(h.m_underLineChar, h.m_title.size()) << "\n\n";
    return s;
}

struct pyClass
{
    explicit pyClass(QAnyStringView name) : m_name(name)  {}

    QAnyStringView m_name;
};

static TextStream &operator<<(TextStream &s, pyClass c)
{
    s << ".. py:class:: " << c.m_name << "\n\n";
    return s;
}

struct currentModule
{
    explicit currentModule(QAnyStringView module) : m_module(module)  {}

    QAnyStringView m_module;
};

static TextStream &operator<<(TextStream &s, const currentModule &m)
{
    s << ".. currentmodule:: " << m.m_module << "\n\n\n";
    return s;
}

DocGeneratorOptions QtDocGenerator::m_options;

QtDocGenerator::QtDocGenerator()
{
    m_options.parameters.snippetComparison =
        ReportHandler::debugLevel() >= ReportHandler::FullDebug;
}

QtDocGenerator::~QtDocGenerator() = default;

QString QtDocGenerator::fileNameSuffix()
{
    return u".rst"_s;
}

bool QtDocGenerator::shouldGenerate(const TypeEntryCPtr &te) const
{
    return Generator::shouldGenerate(te)
        && te->type() != TypeEntry::SmartPointerType;
}

QString QtDocGenerator::fileNameForContext(const GeneratorContext &context) const
{
    return fileNameForContextHelper(context, fileNameSuffix(),
                                    FileNameFlag::UnqualifiedName
                                    | FileNameFlag::KeepCase);
}

void QtDocGenerator::writeFormattedBriefText(TextStream &s, const Documentation &doc,
                                             const QString &scope,
                                             QtXmlToSphinxImages *images) const
{
    writeFormattedText(s, doc.brief(), doc.format(), {}, scope, images);
}

void QtDocGenerator::writeFormattedDetailedText(TextStream &s, const Documentation &doc,
                                                const QtXmlToSphinxOptions &options, const QString &scope,
                                                QtXmlToSphinxImages *images) const
{
    writeFormattedText(s, doc.detailed(), doc.format(), options, scope, images);
}

void QtDocGenerator::writeFormattedText(TextStream &s, const QString &doc,
                                        DocumentationFormat format, const QtXmlToSphinxOptions &options,
                                        const QString &scope,
                                        QtXmlToSphinxImages *images) const
{
    if (format == DocumentationFormat::Native) {
        QtXmlToSphinx x(this, m_options.parameters, options, doc, scope);
        s << x;
        images->append(x.images());
    } else {
        const auto lines = QStringView{doc}.split(u'\n');
        int typesystemIndentation = std::numeric_limits<int>::max();
        // check how many spaces must be removed from the beginning of each line
        for (const auto &line : lines) {
            const auto it = std::find_if(line.cbegin(), line.cend(),
                                         [] (QChar c) { return !c.isSpace(); });
            if (it != line.cend())
                typesystemIndentation = qMin(typesystemIndentation, int(it - line.cbegin()));
        }
        if (typesystemIndentation == std::numeric_limits<int>::max())
            typesystemIndentation = 0;
        for (const auto &line : lines) {
            s << (typesystemIndentation > 0 && typesystemIndentation < line.size()
                    ? line.right(line.size() - typesystemIndentation) : line)
                << '\n';
        }
    }

    s << '\n';
}

static void writeInheritanceList(TextStream &s, const AbstractMetaClassCList& classes,
                                 const char *label)
{
    s << "**" << label << ":** ";
    for (qsizetype i = 0, size = classes.size(); i < size; ++i) {
        if (i > 0)
            s << ", ";
        s << classRef(classes.at(i)->fullName());
    }
    s << "\n\n";
}

static void writeInheritedByList(TextStream &s, const AbstractMetaClassCPtr &metaClass,
                                 const AbstractMetaClassCList& allClasses)
{
    AbstractMetaClassCList res;
    for (const auto &c : allClasses) {
        if (c != metaClass && inheritsFrom(c, metaClass))
            res << c;
    }

    if (!res.isEmpty())
        writeInheritanceList(s, res, "Inherited by");
}

static void writeInheritedFromList(TextStream &s, const AbstractMetaClassCPtr &metaClass)
{
    AbstractMetaClassCList res;

    recurseClassHierarchy(metaClass, [&res, metaClass](const AbstractMetaClassCPtr &c) {
        if (c.get() != metaClass.get())
            res.append(c);
        return false;
    });

    if (!res.isEmpty())
        writeInheritanceList(s, res, "Inherits from");
}

void QtDocGenerator::generateClass(TextStream &s, const QString &targetDir,
                                   const GeneratorContext &classContext,
                                   QList<GeneratorContext> *contexts)
{
    // PYSIDE-2689: Write a (dummy) headline to fix the search contexts.
    s << headline(classContext.metaClass()->fullName(), '#');
    generateClassRecursion(s, targetDir, classContext, contexts);
}

static inline TypeSystem::DocMode classDocMode(const AbstractMetaClassCPtr &metaClass)
{
    return typeSystemTypeEntry(metaClass->typeEntry())->docMode();
}

void QtDocGenerator::generateClassRecursion(TextStream &s, const QString &targetDir,
                                            const GeneratorContext &classContext,
                                            QList<GeneratorContext> *contexts)
{
    const AbstractMetaClassCPtr &metaClass = classContext.metaClass();

    qCDebug(lcShibokenDoc, "Generating Documentation for %s", qPrintable(metaClass->fullName()));

    const bool nested = metaClass->enclosingClass() != nullptr;
    m_packages[metaClass->package()].classPages.append({metaClass->name(), metaClass->fullName(),
                                                        fileNameForContext(classContext), nested});

    doGenerateClass(s, targetDir, metaClass);

    if (classDocMode(metaClass) == TypeSystem::DocMode::Nested) {
        QList<GeneratorContext> innerContexts;
        for (qsizetype i = 0; i < contexts->size(); ) {
            if (contexts->at(i).metaClass()->targetLangEnclosingClass() == metaClass)
                innerContexts.append(contexts->takeAt(i));
            else
                ++i;
        }
        if (!innerContexts.isEmpty()) {
            s << indent;
            for (const auto &innerClassContext : std::as_const(innerContexts))
                generateClassRecursion(s, targetDir, innerClassContext, contexts);
            s << outdent;
        }
    }
}

void QtDocGenerator::writeDetailedDescription(TextStream &s,
                                              const AbstractMetaClassCPtr &metaClass,
                                              const QString &scope,
                                              QtXmlToSphinxImages *parsedImages) const
{
    auto documentation = metaClass->documentation();
    writeInjectDocumentation(s, TypeSystem::DocModificationPrepend, metaClass,
                             parsedImages);
    if (!writeInjectDocumentation(s, TypeSystem::DocModificationReplace, metaClass,
                                  parsedImages)) {
        QtXmlToSphinxOptions options;
        options.headingOffset = 3;
        writeFormattedDetailedText(s, documentation, options, scope, parsedImages);
    }
    writeInjectDocumentation(s, TypeSystem::DocModificationAppend, metaClass,
                             parsedImages);
}

enum ClassDescriptionMode : std::uint8_t
{
    NoDescription,
    BriefOnly,
    DetailedOnly,
    BriefAndDetailed,
    BriefAndDetailedSections,
};

static ClassDescriptionMode classDescriptionMode(const Documentation &doc)
{
    if (!doc.hasDetailed())
        return doc.hasBrief() ? BriefOnly : NoDescription;
    if (!doc.hasBrief())
        return DetailedOnly;
    return doc.detailed().contains("<section"_L1)
        ? BriefAndDetailedSections : BriefAndDetailed;
}

void QtDocGenerator::doGenerateClass(TextStream &s, const QString &targetDir,
                                     const AbstractMetaClassCPtr &metaClass)
{
    qCDebug(lcShibokenDoc).noquote().nospace() << "Generating Documentation for " << metaClass->fullName();

    const QString sourceFile =
        m_docParser->fillDocumentation(std::const_pointer_cast<AbstractMetaClass>(metaClass));

    s << currentModule(metaClass->package()) << pyClass(metaClass->name());
    Indentation indent(s);

    QtXmlToSphinxImages parsedImages;
    auto documentation = metaClass->documentation();
    const QString scope = classScope(metaClass);

    const auto descriptionMode = classDescriptionMode(documentation);
    switch (descriptionMode) {
    case NoDescription:
    case DetailedOnly:
        break;
    case BriefOnly:
        writeFormattedBriefText(s, documentation, scope, &parsedImages);
        break;
    case BriefAndDetailed: {
        // A "collapse" sphinx directive can be used for brief/expanding to details
        // for descriptions consisting of a paragraph sequence.
        writeFormattedBriefText(s, documentation, scope, &parsedImages);
        s << "\n\n.. collapse:: Details\n\n";
        Indentation detailIndent(s);
        writeDetailedDescription(s, metaClass, scope, &parsedImages);
    }
        break;
    case BriefAndDetailedSections: {
        // If the the description has nested <section>'s (which break collapse::), we
        // use a 'more' label for the detailed text to be written further down.
        QString brief = documentation.brief();
        brief.insert(brief.lastIndexOf(u'<'), "<rst> More_...</rst>"_L1);
        writeFormattedText(s, brief, documentation.format(), {}, scope, &parsedImages);
    }
        break;
    }

    if (!metaClass->baseClasses().isEmpty()) {
        if (m_options.inheritanceDiagram) {
            s << ".. inheritance-diagram:: " << metaClass->fullName()<< '\n'
              << "    :parts: 2\n\n";
        } else {
            writeInheritedFromList(s, metaClass);
        }
    }

    writeInheritedByList(s, metaClass, api().classes());

    const auto version = versionOf(metaClass->typeEntry());
    if (!version.isNull())
        s << rstVersionAdded(version);
    if (metaClass->attributes().testFlag(AbstractMetaClass::Deprecated))
        s << rstDeprecationNote("class");

    const GeneratorDocumentation doc = generatorDocumentation(metaClass);

    if (!doc.allFunctions.isEmpty() || !doc.properties.isEmpty()) {
        s << '\n' << headline("Synopsis");
        writePropertyToc(s, doc);
        writeFunctionToc(s, u"Methods"_s, doc.tocNormalFunctions);
        writeFunctionToc(s, u"Virtual methods"_s, doc.tocVirtuals);
        writeFunctionToc(s, u"Slots"_s, doc.tocSlotFunctions);
        writeFunctionToc(s, u"Signals"_s, doc.tocSignalFunctions);
        writeFunctionToc(s, u"Class methods"_s, doc.tocClassFunctions);
        writeFunctionToc(s, u"Static functions"_s, doc.tocStaticFunctions);
    }

    s << "\n.. note::\n"
         "    This documentation may contain snippets that were automatically\n"
         "    translated from C++ to Python. We always welcome contributions\n"
         "    to the snippet translation. If you see an issue with the\n"
         "    translation, you can also let us know by creating a ticket on\n"
         "    https:/bugreports.qt.io/projects/PYSIDE\n\n";

    switch (descriptionMode) {
    case DetailedOnly:
    case BriefAndDetailedSections:
        s << '\n' << headline("Detailed Description");
        if (descriptionMode == BriefAndDetailedSections)
            s << ".. _More:\n";
        writeDetailedDescription(s, metaClass, scope, &parsedImages);
        break;
    default:
        break;
    }

    writeEnums(s, metaClass->enums(), scope, &parsedImages);

    if (!doc.properties.isEmpty())
        writeProperties(s, doc, metaClass, &parsedImages);

    if (!metaClass->isNamespace())
        writeFields(s, metaClass, &parsedImages);

    writeFunctions(s, doc.allFunctions, metaClass, scope, &parsedImages);

    copyParsedImages(parsedImages, {sourceFile}, targetDir);
}

void QtDocGenerator::writeFunctionToc(TextStream &s, const QString &title,
                                      const AbstractMetaFunctionCList &functions)
{
    if (!functions.isEmpty()) {
        s << headline(title, '^')
          << ".. container:: function_list\n\n" << indent;
        // Functions are sorted by the Metabuilder; erase overloads
        QStringList toc;
        toc.reserve(functions.size());
        std::transform(functions.cbegin(), functions.end(),
                       std::back_inserter(toc), getFuncName);
        toc.erase(std::unique(toc.begin(), toc.end()), toc.end());
        for (const auto &func : toc)
            s << "* def " << functionRef(func) << '\n';
        s << outdent << "\n\n";
    }
}

void QtDocGenerator::writePropertyToc(TextStream &s,
                                      const GeneratorDocumentation &doc)
{
    if (doc.properties.isEmpty())
        return;

    s << headline("Properties", '^')
        << ".. container:: function_list\n\n" << indent;
    for (const auto &prop : doc.properties) {
        s << "* " << propRef(propertyRefTarget(prop.name));
        if (prop.documentation.hasBrief())
            s << " - " << prop.documentation.brief();
        s << '\n';
    }
    s << outdent << "\n\n";
}

void QtDocGenerator::writeProperties(TextStream &s,
                                     const GeneratorDocumentation &doc,
                                     const AbstractMetaClassCPtr &cppClass,
                                     QtXmlToSphinxImages *images) const
{
    s << "\n.. note:: Properties can be used directly when "
        << "``from __feature__ import true_property`` is used or via accessor "
        << "functions otherwise.\n\n";

    const QString scope = classScope(cppClass);
    for (const auto &prop : doc.properties) {
        const QString type = translateToPythonType(prop.type, cppClass, /* createRef */ false);
        s <<  ".. py:property:: " << propertyRefTarget(prop.name)
            << "\n   :type: " << type << "\n\n\n";
        if (!prop.documentation.isEmpty()) {
            writeFormattedText(s, prop.documentation.detailed(), DocumentationFormat::Native, {},
                               scope, images);
        }
        s << "**Access functions:**\n";
        if (prop.getter)
            s << " * " << functionRef(prop.getter->name()) << '\n';
        if (prop.setter)
            s << " * " << functionRef(prop.setter->name()) << '\n';
        if (prop.reset)
            s << " * " << functionRef(prop.reset->name()) << '\n';
        if (prop.notify)
            s << " * Signal " << functionRef(prop.notify->name()) << '\n';
        s << '\n';
    }
}

void QtDocGenerator::writeEnums(TextStream &s, const AbstractMetaEnumList &enums,
                                const QString &scope, QtXmlToSphinxImages *images) const
{
    for (const AbstractMetaEnum &en : enums) {
        s << pyClass(en.name());
        Indentation indent(s);
        writeFormattedDetailedText(s, en.documentation(), {}, scope, images);
        const auto version = versionOf(en.typeEntry());
        if (!version.isNull())
            s << rstVersionAdded(version);
    }

}

void QtDocGenerator::writeFields(TextStream &s, const AbstractMetaClassCPtr &cppClass,
                                 QtXmlToSphinxImages *images) const
{
    constexpr auto section_title = ".. attribute:: "_L1;

    const QString scope = classScope(cppClass);
    for (const AbstractMetaField &field : cppClass->fields()) {
        s << section_title << cppClass->fullName() << "." << field.name() << "\n\n";
        writeFormattedDetailedText(s, field.documentation(), {}, scope, images);
    }
}

QString QtDocGenerator::formatArgs(const AbstractMetaFunctionCPtr &func)
{
    QString ret = u"("_s;
    int optArgs = 0;

    const AbstractMetaArgumentList &arguments = func->arguments();
    for (const AbstractMetaArgument &arg : arguments) {

        if (arg.isModifiedRemoved())
            continue;

        bool thisIsoptional = !arg.defaultValueExpression().isEmpty();
        if (optArgs || thisIsoptional) {
            ret += u'[';
            optArgs++;
        }

        if (arg.argumentIndex() > 0)
            ret += u", "_s;

        ret += arg.name();

        if (thisIsoptional) {
            QString defValue = arg.defaultValueExpression();
            if (defValue == u"QString()") {
                defValue = u"\"\""_s;
            } else if (defValue == u"QStringList()"
                       || defValue.startsWith(u"QVector")
                       || defValue.startsWith(u"QList")) {
                defValue = u"list()"_s;
            } else if (defValue == u"QVariant()") {
                defValue = none;
            } else {
                defValue.replace(u"::"_s, u"."_s);
                if (defValue == u"nullptr")
                    defValue = none;
                else if (defValue == u"0" && arg.type().isObject())
                    defValue = none;
            }
            ret += u'=' + defValue;
        }
    }

    ret += QString(optArgs, u']') + u')';
    return ret;
}

void QtDocGenerator::writeDocSnips(TextStream &s,
                                 const CodeSnipList &codeSnips,
                                 TypeSystem::CodeSnipPosition position,
                                 TypeSystem::Language language)
{
    Indentation indentation(s);
    static const QStringList invalidStrings{u"*"_s, u"//"_s, u"/*"_s, u"*/"_s};
    const static QString startMarkup = u"[sphinx-begin]"_s;
    const static QString endMarkup = u"[sphinx-end]"_s;

    for (const CodeSnip &snip : codeSnips) {
        if ((snip.position != position) ||
            !(snip.language & language))
            continue;

        QString code = snip.code();
        while (code.contains(startMarkup) && code.contains(endMarkup)) {
            const auto startBlock = code.indexOf(startMarkup) + startMarkup.size();
            const auto endBlock = code.indexOf(endMarkup);

            if ((startBlock == -1) || (endBlock == -1))
                break;

            QString codeBlock = code.mid(startBlock, endBlock - startBlock);
            const QStringList rows = codeBlock.split(u'\n');
            int currentRow = 0;
            qsizetype offset = 0;

            for (QString row : rows) {
                for (const QString &invalidString : std::as_const(invalidStrings))
                    row.remove(invalidString);

                if (row.trimmed().size() == 0) {
                    if (currentRow == 0)
                        continue;
                    s << '\n';
                }

                if (currentRow == 0) {
                    //find offset
                    for (auto c : row) {
                        if (c == u' ')
                            offset++;
                        else if (c == u'\n')
                            offset = 0;
                        else
                            break;
                    }
                }
                s << QStringView{row}.mid(offset) << '\n';
                currentRow++;
            }

            code = code.mid(endBlock+endMarkup.size());
        }
    }
}

void QtDocGenerator::writeFormattedText(TextStream &s, const DocModification &mod,
                                        const QString &scope, QtXmlToSphinxImages *images) const
{
    const bool note = mod.emphasis() == DocumentationEmphasis::LanguageNote;
    if (note)
        s << ".. admonition:: Python Language Note\n\n" << indent;
    writeFormattedText(s, mod.code(), mod.format(), {}, scope, images);
    if (note)
        s << outdent;
}

bool QtDocGenerator::writeDocModifications(TextStream &s,
                                           const DocModificationList &mods,
                                           TypeSystem::DocModificationMode mode,
                                           const QString &scope,
                                           QtXmlToSphinxImages *images) const
{
    bool didSomething = false;
    for (const DocModification &mod : mods) {
        if (mod.target() == DocumentationTarget::Documentation && mod.mode() == mode) {
            writeFormattedText(s, mod, scope, images);
            didSomething = true;
        }
    }
    return didSomething;
}

bool QtDocGenerator::writeInjectDocumentation(TextStream &s,
                                              TypeSystem::DocModificationMode mode,
                                              const AbstractMetaClassCPtr &cppClass,
                                              QtXmlToSphinxImages *images) const
{
    const bool didSomething =
        writeDocModifications(s, DocParser::getDocModifications(cppClass),
                              mode, classScope(cppClass), images);
    s << '\n';

    // FIXME PYSIDE-7: Deprecate the use of doc string on glue code.
    //       This is pre "add-function" and "inject-documentation" tags.
    const TypeSystem::CodeSnipPosition pos = mode == TypeSystem::DocModificationPrepend
        ? TypeSystem::CodeSnipPositionBeginning : TypeSystem::CodeSnipPositionEnd;
    writeDocSnips(s, cppClass->typeEntry()->codeSnips(), pos, TypeSystem::TargetLangCode);
    return didSomething;
}

bool QtDocGenerator::writeInjectDocumentation(TextStream &s,
                                              TypeSystem::DocModificationMode mode,
                                              const DocModificationList &modifications,
                                              const AbstractMetaFunctionCPtr &func,
                                              const QString &scope,
                                              QtXmlToSphinxImages *images) const
{
    const bool didSomething = writeDocModifications(s, modifications, mode, scope, images);
    s << '\n';

    // FIXME PYSIDE-7: Deprecate the use of doc string on glue code.
    //       This is pre "add-function" and "inject-documentation" tags.
    const TypeSystem::CodeSnipPosition pos = mode == TypeSystem::DocModificationPrepend
        ? TypeSystem::CodeSnipPositionBeginning : TypeSystem::CodeSnipPositionEnd;
    writeDocSnips(s, func->injectedCodeSnips(), pos, TypeSystem::TargetLangCode);
    return didSomething;
}

// Reference with a custom name
static QString inline toRef(const QString &fullName, const QString &displayName)
{
    return ":class:`"_L1 +  displayName + u'<' + fullName + ">`"_L1;
}

// Standard reference using unqualified class name
static QString inline toRef(const QString &t)
{
    return ":class:`~"_L1 + t + u'`';
}

QString QtDocGenerator::translateToPythonType(const AbstractMetaType &type,
                                              const AbstractMetaClassCPtr &cppClass,
                                              bool createRef) const
{
    static const QStringList nativeTypes =
        {boolT, floatT, intT, pyObjectT, pyStrT};

    QString name = type.name();
    if (nativeTypes.contains(name))
        return name;

    if (type.typeUsagePattern() == AbstractMetaType::PrimitivePattern) {
        const auto &basicName = type.basicPrimitiveName();
        if (AbstractMetaType::cppSignedIntTypes().contains(basicName)
            || AbstractMetaType::cppUnsignedIntTypes().contains(basicName)) {
            if (basicName == charT)
                return u"single character string"_s;
            return intT;
        }
        if (AbstractMetaType::cppFloatTypes().contains(basicName))
            return floatT;
    }

    static const QSet<QString> stringTypes = {
        u"uchar"_s, u"std::string"_s, u"std::wstring"_s,
        u"std::stringview"_s, u"std::wstringview"_s,
        qStringT, u"QStringView"_s, u"QAnyStringView"_s, u"QUtf8StringView"_s
    };
    if (stringTypes.contains(name))
        return pyStrT;

    static const QHash<QString, QString> typeMap = {
        { cPyObjectT, pyObjectT },
        { u"QStringList"_s, u"list of strings"_s },
        { qVariantT, pyObjectT }
    };
    const auto found = typeMap.constFind(name);
    if (found != typeMap.cend())
        return found.value();

    if (type.isFlags()) {
        const auto fte = std::static_pointer_cast<const FlagsTypeEntry>(type.typeEntry());
        auto enumTypeEntry = fte->originator();
        auto enumName = enumTypeEntry->targetLangName();
        if (createRef)
            enumName.prepend(enumTypeEntry->targetLangPackage() + u'.');
        return "Combination of "_L1 + (createRef ? toRef(enumName) : enumName);
    }
    if (type.isEnum()) {
        auto enumTypeEntry = std::static_pointer_cast<const EnumTypeEntry>(type.typeEntry());
        auto enumName = enumTypeEntry->targetLangName();
        if (createRef)
            enumName.prepend(enumTypeEntry->targetLangPackage() + u'.');
        return createRef ? toRef(enumName) : enumName;
    }

    if (type.isConstant() && name == "char"_L1 && type.indirections() == 1)
        return "str"_L1;

    if (type.isContainer()) {
        QString strType = translateType(type, cppClass, Options(ExcludeConst) | ExcludeReference);
        strType.remove(u'*');
        strType.remove(u'>');
        strType.remove(u'<');
        strType.replace(u"::"_s, u"."_s);
        if (strType.contains(u"QList") || strType.contains(u"QVector")) {
            strType.replace(u"QList"_s, u"list of "_s);
            strType.replace(u"QVector"_s, u"list of "_s);
        } else if (strType.contains(u"QHash") || strType.contains(u"QMap")) {
            strType.remove(u"QHash"_s);
            strType.remove(u"QMap"_s);
            QStringList types = strType.split(u',');
            strType = "Dictionary with keys of type %1 and values of type %2."_L1
                      .arg(types[0], types[1]);
        }
        return strType;
    }

    if (type.typeUsagePattern() == AbstractMetaType::SmartPointerPattern) {
        auto ste = std::static_pointer_cast<const SmartPointerTypeEntry>(type.typeEntry());
        const auto &instantiation = type.instantiations().constFirst();
        auto instantiationTe = instantiation.typeEntry();
        if (auto instantiationO = api().findSmartPointerInstantiation(ste, instantiation.typeEntry())) {
            const QString &name = instantiationO->specialized->name();
            return createRef
                ? toRef(instantiation.fullName(), name) // link to the pointee
                : name;
        }
    }

    if (auto k = AbstractMetaClass::findClass(api().classes(), type.typeEntry()))
        return createRef ? toRef(k->fullName()) : k->name();

    return createRef ? toRef(name) : name;
}

QString QtDocGenerator::getFuncName(const AbstractMetaFunctionCPtr &cppFunc)
{
    if (cppFunc->isConstructor())
        return "__init__"_L1;
    QString result = cppFunc->name();
    if (cppFunc->isOperatorOverload()) {
        const QString pythonOperator = Generator::pythonOperatorFunctionName(result);
        if (!pythonOperator.isEmpty())
            return pythonOperator;
    }
    result.replace(u"::"_s, u"."_s);
    return result;
}

void QtDocGenerator::writeParameterType(TextStream &s,
                                        const AbstractMetaClassCPtr &cppClass,
                                        const AbstractMetaArgument &arg) const
{
    s << ":param " << arg.name() << ": "
      << translateToPythonType(arg.type(), cppClass) << '\n';
}

void QtDocGenerator::writeFunctionParametersType(TextStream &s,
                                                 const AbstractMetaClassCPtr &cppClass,
                                                 const AbstractMetaFunctionCPtr &func) const
{
    s << '\n';
    const AbstractMetaArgumentList &funcArgs = func->arguments();
    for (const AbstractMetaArgument &arg : funcArgs) {
        if (!arg.isModifiedRemoved())
            writeParameterType(s, cppClass, arg);
    }

    QString retType;
    if (!func->isConstructor()) {
        // check if the return type was modified
        retType = func->modifiedTypeName();
        if (retType.isEmpty() && !func->isVoid())
            retType = translateToPythonType(func->type(), cppClass);
    }

    if (!retType.isEmpty())
        s << ":rtype: " << retType << '\n';

    s << '\n';
}

static bool containsFunctionDirective(const DocModification &dm)
{
    return dm.mode() != TypeSystem::DocModificationXPathReplace
        && dm.code().contains(".. py:"_L1);
}

void QtDocGenerator::writeFunctions(TextStream &s, const AbstractMetaFunctionCList &funcs,
                                    const AbstractMetaClassCPtr &cppClass, const QString &scope,
                                    QtXmlToSphinxImages *images) const
{
    QString lastName;
    for (const auto &func : funcs) {
        const bool indexed = func->name() != lastName;
        lastName = func->name();
        writeFunction(s, func, images, cppClass, scope, indexed);
    }
}

static const char *funcDirective(const AbstractMetaFunctionCPtr &func)
{
    if (func->ownerClass() == nullptr)
        return ".. py:function::";
    if (func->isStatic())
        return ".. py:staticmethod::";
    if (func->isClassMethod())
        return ".. py:classmethod::";
    return ".. py:method::";
}

void QtDocGenerator::writeFunction(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                   QtXmlToSphinxImages *images,
                                   const AbstractMetaClassCPtr &cppClass,
                                   const QString &scope, bool indexed) const
{
    const auto modifications = DocParser::getDocModifications(func, cppClass);

    // Enable injecting parameter documentation by adding a complete function directive.
    if (std::none_of(modifications.cbegin(), modifications.cend(), containsFunctionDirective)) {
        s << funcDirective(func) << ' ' << getFuncName(func) << formatArgs(func);
        Indentation indentation(s);
        if (!indexed)
            s << "\n:noindex:";
        if (func->cppAttributes().testFlag(FunctionAttribute::Final))
            s << "\n:final:";
        else if (func->isAbstract())
            s << "\n:abstractmethod:";
        s << "\n\n";
        writeFunctionParametersType(s, cppClass, func);
        const auto version = versionOf(func->typeEntry());
        if (!version.isNull())
            s << rstVersionAdded(version);
        if (func->isDeprecated())
            s << rstDeprecationNote("function");
    }

    writeFunctionDocumentation(s, func, modifications, scope, images);

    if (auto propIndex = func->propertySpecIndex(); propIndex >= 0) {
        const QString name = cppClass->propertySpecs().at(propIndex).name();
        const QString target = propertyRefTarget(name);
        if (func->isPropertyReader())
            s << "\nGetter of property " << propRef(target) << " .\n\n";
        else if (func->isPropertyWriter())
            s << "\nSetter of property " << propRef(target) << " .\n\n";
        else if (func->isPropertyResetter())
            s << "\nReset function of property " << propRef(target) << " .\n\n";
        else if (func->attributes().testFlag(AbstractMetaFunction::Attribute::PropertyNotify))
            s << "\nNotification signal of property " << propRef(target) << " .\n\n";
    }
}

void QtDocGenerator::writeFunctionDocumentation(TextStream &s, const AbstractMetaFunctionCPtr &func,
                                                const DocModificationList &modifications,
                                                const QString &scope,
                                                QtXmlToSphinxImages *images) const

{
    writeInjectDocumentation(s, TypeSystem::DocModificationPrepend, modifications,
                             func, scope, images);
    if (!writeInjectDocumentation(s, TypeSystem::DocModificationReplace, modifications,
                                  func, scope, images)) {
        writeFormattedBriefText(s, func->documentation(), scope, images);
        writeFormattedDetailedText(s, func->documentation(), {}, scope, images);
    }
    writeInjectDocumentation(s, TypeSystem::DocModificationAppend, modifications,
                             func, scope, images);
}

using TocMap = QMap<QChar, QStringList>;

// Sort entries for a TOC by first character, dropping the
// leading common Qt prefixes like 'Q'.
static QChar sortKey(const QString &key)
{
    const auto size = key.size();
    if (size >= 2 && (key.at(0) == u'Q' || key.at(0) == u'q')
        && (key.at(1).isUpper() || key.at(1).isDigit())) {
        return key.at(1); // "QClass" -> 'C', "qSin()" -> 'S', 'Q3DSurfaceWidget' -> '3'
    }
    if (size >= 3 && key.startsWith("Q_"_L1))
        return key.at(2).toUpper(); // "Q_ARG" -> 'A'
    if (size >= 4 && key.startsWith("QT_"_L1))
        return key.at(3).toUpper(); // "QT_TR" -> 'T'
    auto idx = 0;
    for (; idx < size && key.at(idx) == u'_'; ++idx) {
    } // "__init__" -> 'I'
    return idx < size ? key.at(idx).toUpper() : u'A';
}

static TocMap classEntryListToToc(const QList<DocClassEntry> &entries,
                                  TypeSystem::DocMode docMode)
{
    const bool fullyQualified = docMode == TypeSystem::DocMode::Nested;
    TocMap result;
    // Sort by name, use full href
    for (const auto &e : entries)
         result[sortKey(e.name)] << (fullyQualified ? e.fullName : e.name);
    return result;
}

static TocMap fileListToToc(const QStringList &items)
{
    TocMap result;
    for (const auto &item : items) {
        const QString entry = fileNameToTocEntry(item);
        result[sortKey(entry)] << entry;
    }
    return result;
}

static TocMap functionListToToc(const AbstractMetaFunctionCList &functions)
{
    TocMap result;
    // Functions are sorted by the Metabuilder; erase overloads
    std::unordered_set<QString> seenNames;
    for (const auto &f : functions) {
        const QString &name = f->name();
        if (seenNames.find(name) == seenNames.end()) {
            seenNames.insert(name);
            result[sortKey(name)] << name;
        }
    }
    return result;
}

static TocMap enumListToToc(const AbstractMetaEnumList &enums)
{
    TocMap result;
    for (const auto &e : enums)
        result[sortKey(e.name())] << e.name();
    return result;
}

static void writeFancyToc(TextStream& s, QAnyStringView title,
                          const TocMap &tocMap,
                          QLatin1StringView referenceType)
{
    if (tocMap.isEmpty())
        return;

    qsizetype maxColumnCount = 0;
    for (auto it = tocMap.cbegin(), end = tocMap.cend(); it != end; ++it) {
        if (it.value().size() > maxColumnCount)
            maxColumnCount = it.value().size();
    }
    const auto columnCount = 1 + std::min(maxColumnCount, qsizetype(3)); // With header

    QtXmlToSphinx::Table table;
    for (auto it = tocMap.cbegin(), end = tocMap.cend(); it != end; ++it) {
        QtXmlToSphinx::TableRow row;
        const QString charEntry = u"**"_s + it.key() + u"**"_s;
        row << QtXmlToSphinx::TableCell(charEntry);
        for (const QString &item : std::as_const(it.value())) {
            if (row.size() >= columnCount) {
                table.appendRow(row);
                row.clear();
                row << QtXmlToSphinx::TableCell(QString{});
            }
            QString entry = "* :"_L1 + referenceType + ":`"_L1;
            if (item.contains(u'.'))
                entry += u'~';
            entry += item + u'`';
            row << QtXmlToSphinx::TableCell(entry);
        }
        if (row.size() > 1) {
            if (const auto padColSpan = columnCount - row.size(); padColSpan > 0) {
                QtXmlToSphinx::TableCell padding(QString{});
                padding.colSpan = int(padColSpan);
                row.append(padding);
            }
            table.appendRow(row);
        }
    }

    table.normalize();
    s << '\n' << headline(title) << ".. container:: pysidetoc\n\n" << indent;
    table.format(s);
    s << outdent;
}

bool QtDocGenerator::finishGeneration()
{
    for (const auto &f : api().globalFunctions()) {
        auto ncf = std::const_pointer_cast<AbstractMetaFunction>(f);
        m_docParser->fillGlobalFunctionDocumentation(ncf);
        m_packages[f->targetLangPackage()].globalFunctions.append(f);
    }

    for (auto e : api().globalEnums()) {
        m_docParser->fillGlobalEnumDocumentation(e);
        m_packages[e.typeEntry()->targetLangPackage()].globalEnums.append(e);
    }

    if (!m_packages.isEmpty())
        writeModuleDocumentation();
    if (!m_options.additionalDocumentationList.isEmpty())
        writeAdditionalDocumentation();
    if (!m_options.inheritanceFile.isEmpty() && !writeInheritanceFile())
        return false;
    return true;
}

bool QtDocGenerator::writeInheritanceFile()
{
    QFile inheritanceFile(m_options.inheritanceFile);
    if (!inheritanceFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(msgCannotOpenForWriting(inheritanceFile));

    QJsonObject dict;
    for (const auto &c : api().classes()) {
        const auto &bases = c->baseClasses();
        if (!bases.isEmpty()) {
            QJsonArray list;
            for (const auto &base : bases)
                list.append(QJsonValue(base->fullName()));
            dict[c->fullName()] = list;
        }
    }
    QJsonDocument document;
    document.setObject(dict);
    inheritanceFile.write(document.toJson(QJsonDocument::Compact));
    return true;
}

// Remove function entries that have extra documentation pages
static inline void removeExtraDocs(const QStringList &extraTocEntries,
                                   AbstractMetaFunctionCList *functions)
{
    auto predicate = [&extraTocEntries](const AbstractMetaFunctionCPtr &f) {
        return extraTocEntries.contains(f->name());
    };
    functions->erase(std::remove_if(functions->begin(),functions->end(), predicate),
                     functions->end());
}

// Extract images from RstDocs
static bool imagesFromRstDocs(const QByteArray &rstDoc, const QString &scope,
                              QtXmlToSphinxImages *images)
{
    bool result = false;
    static const QByteArray imageTag = ".. image:: "_ba;

    for (qsizetype pos = 0; pos < rstDoc.size(); ) {
        pos = rstDoc.indexOf(imageTag, pos);
        if (pos == -1)
            break;
        pos += imageTag.size();
        const auto newLinePos = rstDoc.indexOf('\n', pos);
        if (newLinePos == -1)
            break;
        const auto image = rstDoc.sliced(pos, newLinePos - pos).trimmed();
        images->append({scope, QString::fromUtf8(image)});
        result = true;
        pos = newLinePos + 1;
    }
    return result;
}

void QtDocGenerator::writeModuleDocumentation()
{
    auto *typeDb = TypeDatabase::instance();
    for (auto it = m_packages.begin(), end = m_packages.end(); it != end; ++it) {
        auto &docPackage = it.value();
        std::sort(docPackage.classPages.begin(), docPackage.classPages.end(), classEntryLessThan);

        QString key = it.key();
        key.replace(u'.', u'/');
        QString outputDir = outputDirectory() + u'/' + key;
        FileOut output(outputDir + u"/index.rst"_s);
        QtXmlToSphinxImages parsedImages;
        TextStream& s = output.stream;

        TypeSystemTypeEntryCPtr typeSystemEntry = typeDb->findTypeSystemType(it.key());
        Q_ASSERT(typeSystemEntry);
        const auto docMode = typeSystemEntry->docMode();

        const QString &title = it.key();
        s << ".. module:: " << title << "\n\n" << headline(title, '*');

        // Store the it.key() in a QString so that it can be stripped off unwanted
        // information when neeeded. For example, the RST files in the extras directory
        // doesn't include the PySide# prefix in their names.
        QString moduleName = it.key();
        const auto lastIndex = moduleName.lastIndexOf(u'.');
        if (lastIndex >= 0)
            moduleName.remove(0, lastIndex + 1);

        // Search for extra-sections
        QStringList extraTocEntries;
        if (!m_options.extraSectionDir.isEmpty()) {
            QDir extraSectionDir(m_options.extraSectionDir);
            if (!extraSectionDir.exists()) {
                const QString m = u"Extra sections directory "_s +
                                  m_options.extraSectionDir + u" doesn't exist"_s;
                throw Exception(m);
            }

            // Filter for "QtCore.Property.rst", skipping module doc "QtCore.rst"
            const QString filter = moduleName + u".?*.rst"_s;
            const auto fileList =
                extraSectionDir.entryInfoList({filter}, QDir::Files, QDir::Name);
            for (const auto &fi : fileList)
                readExtraDoc(fi, moduleName, outputDir, &docPackage, &extraTocEntries);
        }

        removeExtraDocs(extraTocEntries, &docPackage.globalFunctions);
        const bool hasGlobals = !docPackage.globalFunctions.isEmpty()
                                || !docPackage.globalEnums.isEmpty();
        const QString globalsPage = moduleName + "_globals.rst"_L1;

        s << ".. container:: hide\n\n" << indent
            << ".. toctree::\n" << indent
            << ":maxdepth: 1\n\n";
        if (hasGlobals)
            s << globalsPage << '\n';
        for (const auto &e : std::as_const(docPackage.classPages)) {
            if (!e.nested || docMode == TypeSystem::DocMode::Flat)
                s << e.file << '\n';
        }
        s << "\n\n" << outdent << outdent << headline("Detailed Description");

        // module doc is always wrong and C++istic, so go straight to the extra directory!
        const QString moduleDocRstFileName = m_options.extraSectionDir + u'/' + moduleName + u".rst"_s;
        QStringList sourceFileNames;

        QString context = it.key();
        // Get the WebXML source file for image resolution if we
        // are re-using images from it in our .rst.
        QtXmlToSphinx::stripPythonQualifiers(&context);
        const ModuleDocumentation moduleDocumentation =
            m_docParser->retrieveModuleDocumentation(it.key());
        const Documentation &webXmlModuleDoc = moduleDocumentation.documentation;
        if (webXmlModuleDoc.hasSourceFile())
            sourceFileNames.append(webXmlModuleDoc.sourceFile());
        if (QFileInfo::exists(moduleDocRstFileName)) {
            QFile moduleDoc(moduleDocRstFileName);
            if (!moduleDoc.open(QIODevice::ReadOnly | QIODevice::Text))
                throw Exception(msgCannotOpenForReading(moduleDoc));
            const QByteArray rstDoc = moduleDoc.readAll();
            s << rstDoc;
            moduleDoc.close();
            if (imagesFromRstDocs(rstDoc, context, &parsedImages))
                sourceFileNames.append(moduleDocRstFileName);
        } else if (!webXmlModuleDoc.isEmpty()) {
            // try the normal way
            if (webXmlModuleDoc.format() == DocumentationFormat::Native) {
                QtXmlToSphinxOptions options;
                options.headingOffset = 3;
                QtXmlToSphinx x(this, m_options.parameters, options, webXmlModuleDoc.detailed(), context);
                s << x;
                parsedImages += x.images();
            } else {
                s << webXmlModuleDoc.detailed();
            }
        }

        if (!moduleDocumentation.qmlTypesUrl.isEmpty()) {
            s << '\n' << headline("List of QML types")
              << "\n    * `" << moduleName<< " QML Types <"
              << moduleDocumentation.qmlTypesUrl << ">`_\n\n";
        }

        writeFancyToc(s, "List of Classes", classEntryListToToc(docPackage.classPages, docMode),
                      "class"_L1);
        writeFancyToc(s, "List of Decorators", fileListToToc(docPackage.decoratorPages),
                      "deco"_L1);
        writeFancyToc(s, "List of Functions", functionListToToc(docPackage.globalFunctions),
                      "py:func"_L1);
        writeFancyToc(s, "List of Enumerations", enumListToToc(docPackage.globalEnums),
                      "any"_L1);

        output.done();
        copyParsedImages(parsedImages, sourceFileNames, outputDir);

        if (hasGlobals)
            writeGlobals(it.key(), outputDir + u'/' + globalsPage, docPackage);
    }
}

void QtDocGenerator::writeGlobals(const QString &package,
                                  const QString &fileName,
                                  const DocPackage &docPackage)
{
    FileOut output(fileName);
    QStringList docFiles;
    QtXmlToSphinxImages parsedImages;
    TextStream &s = output.stream;

    // Write out functions with injected documentation
    if (!docPackage.globalFunctions.isEmpty()) {
        for (const auto &func : docPackage.globalFunctions) {
            const Documentation &doc = func->documentation();
            if (doc.hasSourceFile() && !docFiles.contains(doc.sourceFile()))
                docFiles.append(doc.sourceFile());
        }
        s << currentModule(package) << headline("Functions");
        writeFunctions(s, docPackage.globalFunctions, {}, {}, &parsedImages);
    }

    if (!docPackage.globalEnums.isEmpty()) {
        s << headline("Enumerations");
        for (const auto &globalEnum : docPackage.globalEnums) {
            const Documentation &doc = globalEnum.documentation();
            if (doc.hasSourceFile() && !docFiles.contains(doc.sourceFile()))
                docFiles.append(doc.sourceFile());
        }
        writeEnums(s, docPackage.globalEnums, package, &parsedImages);
    }

    output.done();
    copyParsedImages(parsedImages, docFiles, QFileInfo(fileName).absolutePath());
}

static inline QString msgNonExistentAdditionalDocFile(const QString &dir,
                                                      const QString &fileName)
{
    QString result;
    QTextStream(&result) << "Additional documentation file \""
        << fileName << "\" does not exist in "
        << QDir::toNativeSeparators(dir) << '.';
    return result;
}

// Return rst target document name for additional docs
// "qtcore/webxml/animation.webxml" -> "qtcore-animation.rst"
static QString additionalDocRstFileName(const QFileInfo &fi, const QString &rstSuffix)
{
    QString result = fi.baseName() + rstSuffix;
    // Disambiguate file name by directory
    const QString dirName = fi.absolutePath();
    QStringView prefix{dirName};
    if (prefix.endsWith("/webxml"_L1))
        prefix = prefix.chopped(7);
    auto lastSlash = prefix.lastIndexOf(u'/');
    if (lastSlash != -1) {
        prefix = prefix.sliced(lastSlash + 1);
        if (!result.startsWith(prefix)) {
            result.prepend(u'-');
            result.prepend(prefix);
        }
    }
    return result;
}

void QtDocGenerator::writeAdditionalDocumentation() const
{
    QFile additionalDocumentationFile(m_options.additionalDocumentationList);
    if (!additionalDocumentationFile.open(QIODevice::ReadOnly | QIODevice::Text))
        throw Exception(msgCannotOpenForReading(additionalDocumentationFile));

    QDir outDir(outputDirectory());
    const QString rstSuffix = fileNameSuffix();

    QString errorMessage;
    int successCount = 0;
    int count = 0;

    QString targetDir = outDir.absolutePath();

    while (!additionalDocumentationFile.atEnd()) {
        const QByteArray lineBA = additionalDocumentationFile.readLine().trimmed();
        if (lineBA.isEmpty() || lineBA.startsWith('#'))
            continue;
        const QString line = QFile::decodeName(lineBA);
        // Parse "[directory]" specification
        if (line.size() > 2 && line.startsWith(u'[') && line.endsWith(u']')) {
            const QString dir = line.mid(1, line.size() - 2);
            if (dir.isEmpty() || dir == u".") {
                targetDir = outDir.absolutePath();
            } else {
                if (!outDir.exists(dir) && !outDir.mkdir(dir)) {
                    const QString m = "Cannot create directory "_L1
                                      + dir + " under "_L1
                                      + QDir::toNativeSeparators(outputDirectory());
                    throw Exception(m);
                }
                targetDir = outDir.absoluteFilePath(dir);
            }
        } else {
            // Normal file entry
            QFileInfo fi(m_options.parameters.docDataDir + u'/' + line);
            if (fi.isFile()) {
                const QString rstFileName = additionalDocRstFileName(fi, rstSuffix);
                const QString rstFile = targetDir + u'/' + rstFileName;
                const QString context = targetDir.mid(targetDir.lastIndexOf(u'/') + 1);
                if (convertToRst(fi.absoluteFilePath(),
                                 rstFile, context, &errorMessage)) {
                    ++successCount;
                    qCDebug(lcShibokenDoc).nospace().noquote() << __FUNCTION__
                        << " converted " << fi.fileName()
                        << ' ' << rstFileName;
                } else {
                    qCWarning(lcShibokenDoc, "%s", qPrintable(errorMessage));
                }
            } else {
                // FIXME: This should be an exception, in principle, but it
                // requires building all modules.
                qCWarning(lcShibokenDoc, "%s",
                          qPrintable(msgNonExistentAdditionalDocFile(m_options.parameters.docDataDir, line)));
            }
            ++count;
        }
    }
    additionalDocumentationFile.close();

    qCInfo(lcShibokenDoc, "Created %d/%d additional documentation files.",
           successCount, count);
}

bool QtDocGenerator::doSetup()
{
    if (m_options.parameters.codeSnippetDirs.isEmpty()) {
        m_options.parameters.codeSnippetDirs =
            m_options.parameters.libSourceDir.split(QDir::listSeparator());
    }

    if (!m_docParser) {
        if (m_options.doxygen)
            m_docParser = std::make_unique<DoxygenParser>();
        else
            m_docParser = std::make_unique<QtDocParser>();
    }

    if (m_options.parameters.libSourceDir.isEmpty()
        || m_options.parameters.docDataDir.isEmpty()) {
        qCWarning(lcShibokenDoc) << "Documentation data dir and/or Qt source dir not informed, "
                                 "documentation will not be extracted from Qt sources.";
        return false;
    }

    m_docParser->setDocumentationDataDirectory(m_options.parameters.docDataDir);
    m_options.parameters.outputDirectory = outputDirectory();
    return true;
}

QList<OptionDescription> QtDocGenerator::options()
{
    return {
        {u"doc-parser=<parser>"_s,
         u"The documentation parser used to interpret the documentation\n"
          "input files (qdoc|doxygen)"_s},
        {u"documentation-code-snippets-dir=<dir>"_s,
         u"Directory used to search code snippets used by the documentation"_s},
        {u"snippets-path-rewrite=old:new"_s,
         u"Replacements in code snippet path to find .cpp/.h snippets converted to Python"_s},
        {u"documentation-data-dir=<dir>"_s,
         u"Directory with XML files generated by documentation tool"_s},
        {u"documentation-extra-sections-dir=<dir>"_s,
         u"Directory used to search for extra documentation sections"_s},
        {u"library-source-dir=<dir>"_s,
         u"Directory where library source code is located"_s},
        {additionalDocumentationOption + u"=<file>"_s,
         u"List of additional XML files to be converted to .rst files\n"
          "(for example, tutorials)."_s},
        {u"inheritance-file=<file>"_s,
         u"Generate a JSON file containing the class inheritance."_s},
        {u"disable-inheritance-diagram"_s,
         u"Disable the generation of the inheritance diagram."_s}
    };
}

class QtDocGeneratorOptionsParser : public OptionsParser
{
public:
    explicit QtDocGeneratorOptionsParser(DocGeneratorOptions *o) : m_options(o) {}

    bool handleBoolOption(const QString &key, OptionSource source) override;
    bool handleOption(const QString &key, const QString &value, OptionSource source) override;

private:
    DocGeneratorOptions *m_options;
};

bool QtDocGeneratorOptionsParser::handleBoolOption(const QString &key, OptionSource)
{
    if (key == "disable-inheritance-diagram"_L1) {
        m_options->inheritanceDiagram = false;
        return true;
    }
    return false;
}

bool QtDocGeneratorOptionsParser::handleOption(const QString &key, const QString &value,
                                               OptionSource source)
{
    if (source == OptionSource::CommandLineSingleDash)
        return false;
    if (key == u"library-source-dir") {
        m_options->parameters.libSourceDir = value;
        return true;
    }
    if (key == u"documentation-data-dir") {
        m_options->parameters.docDataDir = value;
        return true;
    }
    if (key == u"documentation-code-snippets-dir") {
        m_options->parameters.codeSnippetDirs = value.split(QDir::listSeparator());
        return true;
    }

    if (key == u"snippets-path-rewrite") {
        const auto pos = value.indexOf(u':');
        if (pos == -1)
            return false;
        m_options->parameters.codeSnippetRewriteOld= value.left(pos);
        m_options->parameters.codeSnippetRewriteNew = value.mid(pos + 1);
        return true;
    }

    if (key == u"documentation-extra-sections-dir") {
        m_options->extraSectionDir = value;
        return true;
    }
    if (key == u"doc-parser") {
        qCDebug(lcShibokenDoc).noquote().nospace() << "doc-parser: " << value;
        if (value == u"doxygen") {
            if (!XQuery::isLibXmlEnabled()) {
                qWarning("doxygen requires libxml/libxslt which was not found.");
                return false;
            }
            m_options->doxygen = true;
        }
        return true;
    }
    if (key == additionalDocumentationOption) {
        m_options->additionalDocumentationList = value;
        return true;
    }

    if (key == u"inheritance-file") {
        m_options->inheritanceFile = value;
        return true;
    }

    return false;
}

std::shared_ptr<OptionsParser> QtDocGenerator::createOptionsParser()
{
    return std::make_shared<QtDocGeneratorOptionsParser>(&m_options);
}

bool QtDocGenerator::convertToRst(const QString &sourceFileName,
                                  const QString &targetFileName,
                                  const QString &context,
                                  QString *errorMessage) const
{
    QFile sourceFile(sourceFileName);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = msgCannotOpenForReading(sourceFile);
        return false;
    }

    FileOut targetFile(targetFileName);
    QtXmlToSphinx x(this, m_options.parameters, {}, sourceFile, context);
    targetFile.stream << x;
    copyParsedImages(x.images(), {sourceFileName},
                     QFileInfo(targetFileName).absolutePath());
    targetFile.done();
    return true;
}

GeneratorDocumentation
    QtDocGenerator::generatorDocumentation(const AbstractMetaClassCPtr &cppClass)
{
    GeneratorDocumentation result;
    const auto allFunctions = cppClass->functions();
    result.allFunctions.reserve(allFunctions.size());
    std::remove_copy_if(allFunctions.cbegin(), allFunctions.cend(),
                        std::back_inserter(result.allFunctions), shouldSkip);

    std::stable_sort(result.allFunctions.begin(), result.allFunctions.end(), functionSort);

    for (const auto &func : std::as_const(result.allFunctions)) {
        if (func->isStatic())
            result.tocStaticFunctions.append(func);
        if (func->isClassMethod())
            result.tocClassFunctions.append(func);
        else if (func->isVirtual())
            result.tocVirtuals.append(func);
        else if (func->isSignal())
            result.tocSignalFunctions.append(func);
        else if (func->isSlot())
            result.tocSlotFunctions.append(func);
        else
            result.tocNormalFunctions.append(func);
    }

    // Find the property getters/setters
    for (const auto &spec: cppClass->propertySpecs()) {
        GeneratorDocumentation::Property property;
        property.name = spec.name();
        property.type = spec.type();
        property.documentation = spec.documentation();
        if (!spec.read().isEmpty())
            property.getter = AbstractMetaFunction::find(result.allFunctions, spec.read());
        if (!spec.write().isEmpty())
            property.setter = AbstractMetaFunction::find(result.allFunctions, spec.write());
        if (!spec.reset().isEmpty())
            property.reset = AbstractMetaFunction::find(result.allFunctions, spec.reset());
        if (!spec.notify().isEmpty())
            property.notify = AbstractMetaFunction::find(result.tocSignalFunctions, spec.notify());
        result.properties.append(property);
    }
    std::sort(result.properties.begin(), result.properties.end());

    return result;
}

// QtXmlToSphinxDocGeneratorInterface
QString QtDocGenerator::expandFunction(const QString &function) const
{
    const auto firstDot = function.indexOf(u'.');
    AbstractMetaClassCPtr metaClass;
    if (firstDot != -1) {
        const auto className = QStringView{function}.left(firstDot);
        for (const auto &cls : api().classes()) {
            if (cls->name() == className) {
                metaClass = cls;
                break;
            }
        }
    }

    return metaClass
        ? metaClass->typeEntry()->qualifiedTargetLangName()
          + function.right(function.size() - firstDot)
        : function;
}

QString QtDocGenerator::expandClass(const QString &context,
                                    const QString &name) const
{
    if (auto typeEntry = TypeDatabase::instance()->findType(name))
        return typeEntry->qualifiedTargetLangName();
    // fall back to the old heuristic if the type wasn't found.
    QString result = name;
    const auto rawlinklist = QStringView{name}.split(u'.');
    QStringList splittedContext = context.split(u'.');
    if (rawlinklist.size() == 1 || rawlinklist.constFirst() == splittedContext.constLast()) {
        splittedContext.removeLast();
        result.prepend(u'~' + splittedContext.join(u'.') + u'.');
    }
    return result;
}

QString QtDocGenerator::resolveContextForMethod(const QString &context,
                                                const QString &methodName) const
{
    const auto currentClass = QStringView{context}.split(u'.').constLast();

    AbstractMetaClassCPtr metaClass;
    for (const auto &cls : api().classes()) {
        if (cls->name() == currentClass) {
            metaClass = cls;
            break;
        }
    }

    if (metaClass) {
        AbstractMetaFunctionCList funcList;
        const auto &methods = metaClass->queryFunctionsByName(methodName);
        for (const auto &func : methods) {
            if (methodName == func->name())
                funcList.append(func);
        }

        AbstractMetaClassCPtr implementingClass;
        for (const auto &func : std::as_const(funcList)) {
            implementingClass = func->implementingClass();
            if (implementingClass->name() == currentClass)
                break;
        }

        if (implementingClass)
            return implementingClass->typeEntry()->qualifiedTargetLangName();
    }

    return u'~' + context;
}

const QLoggingCategory &QtDocGenerator::loggingCategory() const
{
    return lcShibokenDoc();
}

static bool isRelativeHtmlFile(const QString &linkRef)
{
    return !linkRef.startsWith(u"http")
        && (linkRef.endsWith(u".html") || linkRef.contains(u".html#"));
}

// Resolve relative, local .html documents links to doc.qt.io as they
// otherwise will not work and neither be found in the HTML tree.
QtXmlToSphinxLink QtDocGenerator::resolveLink(const QtXmlToSphinxLink &link) const
{
    if (link.type != QtXmlToSphinxLink::Reference || !isRelativeHtmlFile(link.linkRef))
        return link;
    static const QString prefix = "https://doc.qt.io/qt-"_L1
        + QString::number(QT_VERSION_MAJOR) + u'/';
    QtXmlToSphinxLink resolved = link;
    resolved.type = QtXmlToSphinxLink::External;
    resolved.linkRef = prefix + link.linkRef;
    if (resolved.linkText.isEmpty()) {
        resolved.linkText = link.linkRef;
        const qsizetype anchor = resolved.linkText.lastIndexOf(u'#');
        if (anchor != -1)
            resolved.linkText.truncate(anchor);
    }
    return resolved;
}

// Determine image target directory from context,
// "Pyside2.QtGui.QPainter" ->"Pyside2/QtGui" for the rare case of resolving
// an image by scope/module.
// FIXME: Not perfect yet, should have knowledge about namespaces (DataVis3D) or
// nested classes "Pyside2.QtGui.QTouchEvent.QTouchPoint".
static QString imageRelativeTargetDirFromContext(const QString &scope,
                                                 const QString &hrefBase)
{
    QString relativeTargetDir = scope;
    const auto lastDot = relativeTargetDir.lastIndexOf(u'.');
    if (lastDot != -1)
        relativeTargetDir.truncate(lastDot);
    relativeTargetDir.replace(u'.', u'/');
    relativeTargetDir += hrefBase;
    return relativeTargetDir;
}

ResolvedDocImage
QtDocGenerator::resolveImage(const QtXmlToSphinxImage &image,
                             const QStringList &sourceDirs,
                             const QString &targetDir)
{
    QString hrefBase;
    QString hrefName = image.href; // split "images/a.png"
    if (const auto lastSlash = image.href.lastIndexOf(u'/'); lastSlash != -1) {
        hrefName = image.href.sliced(lastSlash + 1);
        hrefBase = u'/' + image.href.sliced(0, lastSlash);
    }

    QStringList candidates;
    // 1st: Resolve relative to document
    for (const auto &sourceDir : sourceDirs) {
        candidates.append(sourceDir + u'/' + image.href);
        if (QFileInfo::exists(candidates.constLast()))
            return {hrefName, candidates.constLast(), targetDir + hrefBase};
    }

    // 2nd: Check in docDataDir (doxygen) FIXME PYSIDE 7: Is this still needed?
    candidates.append(m_options.parameters.docDataDir + u'/' + image.href);
    if (QFileInfo::exists(candidates.constLast())) {
        return {hrefName, candidates.constLast(),
                m_options.parameters.outputDirectory + hrefBase};
    }

    // 3rd: Try to resolve via scope/module (relative to output directory).
    // It should rarely happen since resolution against the source file should
    // mostly work, only for some cases of injected XML documentation.
    if (!m_options.doxygen && !image.scope.isEmpty()) {
        candidates.append(m_options.parameters.docDataDir + u'/' +
                          QtDocParser::qdocModuleDir(image.scope) + u'/' + image.href);
        if (QFileInfo::exists(candidates.constLast())) {
            return {hrefName, candidates.constLast(),
                    m_options.parameters.outputDirectory + u'/'
                    + imageRelativeTargetDirFromContext(image.scope, hrefBase)};
        }
    }

    throw Exception(msgCannotFindImage(image.href, image.scope, candidates));
    return {};
}

static constexpr Qt::CaseSensitivity fileSysCaseSensitivity =
    QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows
        ? Qt::CaseInsensitive : Qt::CaseSensitive;

// Poor man's relative-to-dir function.
static QString relativeTo(const QString &path, const QString &dir)
{
    const auto dirSize = dir.size();
    const auto pathSize = path.size();
    if (dirSize == pathSize && path.compare(dir, fileSysCaseSensitivity) == 0)
        return {};
    if (path.size() > dirSize && path.startsWith(dir, fileSysCaseSensitivity)
        && path.at(dirSize) == u'/') {
        return path.sliced(dirSize + 1);
    }

    throw Exception(msgNotRelative(path, dir));
    return {};
}

// Copy a parsed image from WebXML to doc/base, creating all needed directories
static void copyParsedImage(const ResolvedDocImage &image, QDir &targetDir)
{
    const QString targetFileName = image.targetDir + u'/' + image.name;
    if (QFileInfo::exists(targetFileName))
        return;

    if (!QFileInfo::exists(image.targetDir)
        && !targetDir.mkpath(relativeTo(image.targetDir, targetDir.absolutePath()))) {
        throw Exception(msgCannotCreateDir(image.targetDir));
    }
    QFile source(image.absoluteSourceFilePath);
    if (!source.copy(targetFileName))
        throw Exception(msgCannotCopy(source, targetFileName));
}

// Copy parsed images from WebXML to doc/base
void QtDocGenerator::copyParsedImages(const QtXmlToSphinxImages &images,
                                      const QStringList &sourceDocumentFiles,
                                      const QString &targetDocumentDir)
{
    if (images.isEmpty())
        return;

    QStringList sourceDirs;
    for (const auto &sourceDocumentFile : sourceDocumentFiles) {
        const QString sourceDir = QFileInfo(sourceDocumentFile).absolutePath();
        if (!sourceDirs.contains(sourceDir))
            sourceDirs.append(sourceDir);
    }

    QDir targetDir(targetDocumentDir);

    for (const auto &image : images)
         copyParsedImage(resolveImage(image, sourceDirs, targetDocumentDir), targetDir);
}
