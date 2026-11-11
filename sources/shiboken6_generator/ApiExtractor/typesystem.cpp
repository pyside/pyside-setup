// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "typesystem.h"
#include "arraytypeentry.h"
#include "codesnip.h"
#include "cpptypeentry.h"
#include "complextypeentry.h"
#include "configurabletypeentry.h"
#include "constantvaluetypeentry.h"
#include "containertypeentry.h"
#include "customtypenentry.h"
#include "debughelpers_p.h"
#include "enumtypeentry.h"
#include "enumvaluetypeentry.h"
#include "flagstypeentry.h"
#include "functiontypeentry.h"
#include "include.h"
#include "namespacetypeentry.h"
#include "objecttypeentry.h"
#include "primitivetypeentry.h"
#include "pythontypeentry.h"
#include "smartpointertypeentry.h"
#include "templateargumententry.h"
#include "typedefentry.h"
#include "typesystemtypeentry.h"
#include "valuetypeentry.h"
#include "varargstypeentry.h"
#include "voidtypeentry.h"
#include "abstractmetatype.h"
#include "typedatabase.h"
#include "modifications.h"
#include "sourcelocation.h"


#include <QtCore/qdebug.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qset.h>
#include <QtCore/qvarlengtharray.h>

using namespace Qt::StringLiterals;

static QString buildName(const QString &entryName, const TypeEntryCPtr &parent)
{
    return parent == nullptr || parent->type() == TypeEntry::TypeSystemType
        ? entryName : parent->name() + u"::"_s + entryName;
}

// Access private class as 'd', cf macro Q_D()
#define S_D(Class) auto d = static_cast<Class##Private *>(d_func())

class TypeEntryPrivate
{
public:
    TypeEntryPrivate(const TypeEntryPrivate &) = default; // Enable copy for cloning.
    TypeEntryPrivate &operator=(const TypeEntryPrivate &) = delete;
    TypeEntryPrivate(TypeEntryPrivate &&) = delete;
    TypeEntryPrivate &operator=(TypeEntryPrivate &&) = delete;

    explicit TypeEntryPrivate(const QString &entryName, TypeEntry::Type t, const QVersionNumber &vr,
                              const TypeEntryCPtr &parent);
    virtual ~TypeEntryPrivate() = default;

    QString shortName() const;

    TypeEntryCPtr m_parent;
    QString m_name; // C++ fully qualified
    mutable QString m_cachedShortName; // C++ excluding inline namespaces
    QString m_entryName;
    QString m_targetLangPackage;
    mutable QString m_cachedTargetLangName; // "Foo.Bar"
    mutable QString m_cachedTargetLangEntryName; // "Bar"
    IncludeList m_extraIncludes;
    Include m_include;
    QVersionNumber m_version;
    SourceLocation m_sourceLocation; // XML file
    TypeEntry::CodeGeneration m_codeGeneration = TypeEntry::GenerateCode;
    CustomTypeEntryPtr m_targetLangApiType;
    int m_revision = 0;
    int m_sbkIndex = 0;
    TypeEntry::Type m_type;
    bool m_stream = false;
    bool m_private = false;
    bool m_builtin = false;
    bool m_usesGil = true;
};

TypeEntryPrivate::TypeEntryPrivate(const QString &entryName, TypeEntry::Type t, const QVersionNumber &vr,
                                   const TypeEntryCPtr &parent) :
    m_parent(parent),
    m_name(buildName(entryName, parent)),
    m_entryName(entryName),
    m_version(vr),
    m_type(t)
{
}

TypeEntry::TypeEntry(const QString &entryName, TypeEntry::Type t, const QVersionNumber &vr,
                     const TypeEntryCPtr &parent) :
    TypeEntry(new TypeEntryPrivate(entryName, t, vr, parent))
{
}

TypeEntry::TypeEntry(TypeEntryPrivate *d) : m_d(d)
{
}

TypeEntry::~TypeEntry() = default;

const IncludeList &TypeEntry::extraIncludes() const
{
    return m_d->m_extraIncludes;
}

void TypeEntry::setExtraIncludes(const IncludeList &includes)
{
    m_d->m_extraIncludes = includes;
}

void TypeEntry::addExtraInclude(const Include &newInclude)
{
    if (!m_d->m_extraIncludes.contains(newInclude))
        m_d->m_extraIncludes.append(newInclude);
}

Include TypeEntry::include() const
{
    return m_d->m_include;
}

void TypeEntry::setInclude(const Include &inc)
{
    // This is a workaround for preventing double inclusion of the QSharedPointer implementation
    // header, which does not use header guards. In the previous parser this was not a problem
    // because the Q_QDOC define was set, and the implementation header was never included.
    if (inc.name().endsWith(u"qsharedpointer_impl.h")) {
        QString path = inc.name();
        path.remove(u"_impl"_s);
        m_d->m_include = Include(inc.type(), path);
    } else {
        m_d->m_include = inc;
    }
}

QVersionNumber TypeEntry::version() const
{
    return m_d->m_version;
}

bool isCppPrimitive(const TypeEntryCPtr &e)
{
    if (!e->isPrimitive())
        return false;

    if (e->type() == TypeEntry::VoidType)
        return true;

    PrimitiveTypeEntryCPtr referencedType = basicReferencedTypeEntry(e);
    const QString &typeName = referencedType->name();
    return AbstractMetaType::cppPrimitiveTypes().contains(typeName);
}

TypeEntry::Type TypeEntry::type() const
{
    return m_d->m_type;
}

TypeEntryCPtr TypeEntry::parent() const
{
    return m_d->m_parent;
}

void TypeEntry::setParent(const TypeEntryCPtr &p)
{
    m_d->m_parent = p;
}

bool TypeEntry::isChildOf(const TypeEntryCPtr &p) const
{
    for (auto e = m_d->m_parent; e; e = e->parent()) {
        if (e == p)
            return true;
    }
    return false;
}

TypeSystemTypeEntryCPtr typeSystemTypeEntry(TypeEntryCPtr e)
{
    for (; e; e = e->parent()) {
        if (e->type() == TypeEntry::TypeSystemType)
            return std::static_pointer_cast<const TypeSystemTypeEntry>(e);
    }
    return {};
}

TypeEntryCPtr targetLangEnclosingEntry(const TypeEntryCPtr &e)
{
    auto result = e->parent();
    while (result && result->type() != TypeEntry::TypeSystemType
           && !NamespaceTypeEntry::isVisibleScope(result)) {
        result = result->parent();
    }
    return result;
}

bool TypeEntry::isPrimitive() const
{
    return m_d->m_type == PrimitiveType;
}

bool TypeEntry::isEnum() const
{
    return m_d->m_type == EnumType;
}

bool TypeEntry::isFlags() const
{
    return m_d->m_type == FlagsType;
}

bool TypeEntry::isObject() const
{
    return m_d->m_type == ObjectType;
}

bool TypeEntry::isNamespace() const
{
    return m_d->m_type == NamespaceType;
}

bool TypeEntry::isContainer() const
{
    return m_d->m_type == ContainerType;
}

bool TypeEntry::isSmartPointer() const
{
    return m_d->m_type == SmartPointerType;
}

bool TypeEntry::isUniquePointer() const
{
    if (m_d->m_type != SmartPointerType)
        return false;
    const auto *ste = static_cast<const SmartPointerTypeEntry *>(this);
    return ste->smartPointerType() == TypeSystem::SmartPointerType::Unique;
}

bool TypeEntry::isArray() const
{
    return m_d->m_type == ArrayType;
}

bool TypeEntry::isTemplateArgument() const
{
    return m_d->m_type == TemplateArgumentType;
}

bool TypeEntry::isVoid() const
{
    return m_d->m_type == VoidType;
}

bool TypeEntry::isVarargs() const
{
    return m_d->m_type == VarargsType;
}

bool TypeEntry::isCustom() const
{
    return m_d->m_type == CustomType || m_d->m_type == PythonType;
}

bool TypeEntry::isTypeSystem() const
{
    return m_d->m_type == TypeSystemType;
}

bool TypeEntry::isFunction() const
{
    return m_d->m_type == FunctionType;
}

bool TypeEntry::isEnumValue() const
{
    return m_d->m_type == EnumValue;
}

bool TypeEntry::stream() const
{
    return m_d->m_stream;
}

void TypeEntry::setStream(bool b)
{
    m_d->m_stream = b;
}

bool TypeEntry::isBuiltIn() const
{
    return m_d->m_builtin;
}

void TypeEntry::setBuiltIn(bool b)
{
    m_d->m_builtin = b;
}

bool TypeEntry::isPrivate() const
{
    return m_d->m_private;
}

void TypeEntry::setPrivate(bool b)
{
    m_d->m_private = b;
}

QString TypeEntry::name() const
{
    return m_d->m_name;
}

// Build the C++ name excluding any inline namespaces
// ("std::__1::shared_ptr" -> "std::shared_ptr"
QString TypeEntryPrivate::shortName() const
{
    if (m_cachedShortName.isEmpty()) {
        QVarLengthArray<TypeEntryCPtr > parents;
        bool foundInlineNamespace = false;
        for (auto p = m_parent; p != nullptr && p->type() != TypeEntry::TypeSystemType; p = p->parent()) {
            if (p->type() == TypeEntry::NamespaceType
                && std::static_pointer_cast<const NamespaceTypeEntry>(p)->isInlineNamespace()) {
                foundInlineNamespace = true;
            } else {
                parents.append(p);
            }
        }
        if (foundInlineNamespace) {
            m_cachedShortName.reserve(m_name.size());
            for (auto i = parents.size() - 1; i >= 0; --i) {
                m_cachedShortName.append(parents.at(i)->entryName());
                m_cachedShortName.append(u"::"_s);
            }
            m_cachedShortName.append(m_entryName);
        } else {
            m_cachedShortName = m_name;
        }
    }
    return m_cachedShortName;
}

QString TypeEntry::shortName() const
{
    return m_d->shortName();
}

QString TypeEntry::entryName() const
{
    return m_d->m_entryName;
}

TypeEntry::CodeGeneration TypeEntry::codeGeneration() const
{
    return m_d->m_codeGeneration;
}

void TypeEntry::setCodeGeneration(TypeEntry::CodeGeneration cg)
{
    m_d->m_codeGeneration = cg;
}

bool TypeEntry::generateCode() const
{
    return m_d->m_codeGeneration == GenerateCode;
}

bool TypeEntry::shouldGenerate() const
{
    return generateCode() && NamespaceTypeEntry::isVisibleScope(this);
}

int TypeEntry::revision() const
{
    return m_d->m_revision;
}

void TypeEntry::setSbkIndex(int i)
{
    m_d->m_sbkIndex = i;
}

QString TypeEntry::qualifiedCppName() const
{
    return m_d->m_name;
}

CustomTypeEntryCPtr TypeEntry::targetLangApiType() const
{
    return m_d->m_targetLangApiType;
}

bool TypeEntry::hasTargetLangApiType() const
{
    return m_d->m_targetLangApiType != nullptr;
}

void TypeEntry::setTargetLangApiType(const CustomTypeEntryPtr &cte)
{
    m_d->m_targetLangApiType = cte;
}

QString TypeEntry::targetLangApiName() const
{
    return m_d->m_targetLangApiType != nullptr
           ? m_d->m_targetLangApiType->name() : m_d->m_name;
}

QString TypeEntry::targetLangName() const
{
    if (m_d->m_cachedTargetLangName.isEmpty())
        m_d->m_cachedTargetLangName = buildTargetLangName();
    return m_d->m_cachedTargetLangName;
}

void TypeEntry::setTargetLangName(const QString &n)
{
    m_d->m_cachedTargetLangName = n;
}

QString TypeEntry::buildTargetLangName() const
{
    QString result = m_d->m_entryName;
    for (auto p = parent(); p && p->type() != TypeEntry::TypeSystemType; p = p->parent()) {
        if (NamespaceTypeEntry::isVisibleScope(p)) {
            if (!result.isEmpty())
                result.prepend(u'.');
            QString n = p->m_d->m_entryName;
            n.replace(u"::"_s, u"."_s); // Primitive types may have "std::"
            result.prepend(n);
        }
    }
    return result;
}

bool TypeEntry::setRevisionHelper(int r)
{
    const bool changed = m_d->m_revision != r;
    m_d->m_revision = r;
    return changed;
}

int TypeEntry::sbkIndexHelper() const
{
    return m_d->m_sbkIndex;
}

SourceLocation TypeEntry::sourceLocation() const
{
    return m_d->m_sourceLocation;
}

void TypeEntry::setSourceLocation(const SourceLocation &sourceLocation)
{
    m_d->m_sourceLocation = sourceLocation;
}

bool isUserPrimitive(const TypeEntryCPtr &e)
{
    if (!e->isPrimitive())
        return false;
    const auto type = basicReferencedTypeEntry(e);
    return !isCppPrimitive(type)
        && type->qualifiedCppName() != u"std::string";
}

bool TypeEntry::isWrapperType() const
{
  return isObject() || isValue() || isSmartPointer();
}

bool isCppIntegralPrimitive(const TypeEntryCPtr &e)
{
    if (!isCppPrimitive(e))
        return false;
    const auto type = basicReferencedTypeEntry(e);
    return AbstractMetaType::cppIntegralTypes().contains(type->qualifiedCppName());
}

bool isExtendedCppPrimitive(const TypeEntryCPtr &e)
{
    if (isCppPrimitive(e))
        return true;
    if (!e->isPrimitive())
        return false;
    const auto type = basicReferencedTypeEntry(e);
    const QString &name = type->qualifiedCppName();
    return name == u"std::string" || name == u"std::wstring";
}

const TypeEntryPrivate *TypeEntry::d_func() const
{
    return m_d.get();
}

TypeEntryPrivate *TypeEntry::d_func()
{
    return m_d.get();
}

QString TypeEntry::targetLangEntryName() const
{
    if (m_d->m_cachedTargetLangEntryName.isEmpty()) {
        m_d->m_cachedTargetLangEntryName = targetLangName();
        const auto lastDot = m_d->m_cachedTargetLangEntryName.lastIndexOf(u'.');
        if (lastDot != -1)
            m_d->m_cachedTargetLangEntryName.remove(0, lastDot + 1);
    }
    return m_d->m_cachedTargetLangEntryName;
}

QString TypeEntry::targetLangPackage() const
{
    return m_d->m_targetLangPackage;
}

void TypeEntry::setTargetLangPackage(const QString &p)
{
    m_d->m_targetLangPackage = p;
}

QString TypeEntry::qualifiedTargetLangName() const
{
    return targetLangPackage() + u'.' + targetLangName();
}

bool TypeEntry::isValue() const
{
    return false;
}

bool TypeEntry::isComplex() const
{
    return false;
}

TypeEntry *TypeEntry::clone() const
{
    return new TypeEntry(new TypeEntryPrivate(*m_d));
}

// Take over parameters relevant for typedefs
void TypeEntry::useAsTypedef(const TypeEntryCPtr &source)
{
    // XML Typedefs are in the global namespace for now.
    m_d->m_parent = typeSystemTypeEntry(source);
    m_d->m_entryName = source->m_d->m_entryName;
    m_d->m_name = source->m_d->m_name;
    m_d->m_targetLangPackage = source->m_d->m_targetLangPackage;
    m_d->m_cachedTargetLangName.clear(); // Clear cached names.
    m_d->m_cachedTargetLangEntryName.clear();
    m_d->m_codeGeneration = source->m_d->m_codeGeneration;
    m_d->m_version = source->m_d->m_version;
}

// ----------------- CppTypeEntry

class CppTypeEntryPrivate : public TypeEntryPrivate
{
public:
    using TypeEntryPrivate::TypeEntryPrivate;

    CppTypeEntryCPtr m_viewOn;
    QString m_defaultConstructor;
    TypeSystem::CopyableFlag m_copyableFlag = TypeSystem::CopyableFlag::Unspecified;
    bool m_copyableDetected = true;
    TypeSystem::DefaultConstructibleFlag m_defaultConstructibleFlag = TypeSystem::DefaultConstructibleFlag::Unspecified;
    bool m_defaultConstructibleDetected = true;
    TypeSystem::MovableFlag m_movableFlag = TypeSystem::MovableFlag::Unspecified;
    bool m_movableDetected = true;
    TypeSystem::DestructibleFlag m_destructibleFlag = TypeSystem::DestructibleFlag::Unspecified;
    TypeSystem::DestructorType m_detectedDestructor = TypeSystem::DestructorType::PublicDestructor;
    TypeSystem::QtMetaTypeRegistration m_qtMetaTypeRegistration =
        TypeSystem::QtMetaTypeRegistration::Unspecified;
};

CppTypeEntry::CppTypeEntry(const QString &entryName, Type t,
                           const QVersionNumber &vr,
                           const TypeEntryCPtr &parent) :
    TypeEntry(new CppTypeEntryPrivate(entryName, t, vr, parent))
{
}

CppTypeEntry::CppTypeEntry(CppTypeEntryPrivate *d) :
    TypeEntry(d)
{
}

const QString &CppTypeEntry::defaultConstructor() const
{
    S_D(const CppTypeEntry);
    return d->m_defaultConstructor;
}

void CppTypeEntry::setDefaultConstructor(const QString& defaultConstructor)
{
    S_D(CppTypeEntry);
    d->m_defaultConstructor = defaultConstructor;
}

CppTypeEntryCPtr CppTypeEntry::viewOn() const
{
    S_D(const CppTypeEntry);
    return d->m_viewOn;
}

void CppTypeEntry::setViewOn(const CppTypeEntryCPtr &v)
{
    S_D(CppTypeEntry);
    d->m_viewOn = v;
}

TypeSystem::DefaultConstructibleFlag CppTypeEntry::defaultConstructibleFlag() const
{
    S_D(const CppTypeEntry);
    return d->m_defaultConstructibleFlag;
}

void CppTypeEntry::setDefaultConstructibleFlag(TypeSystem::DefaultConstructibleFlag flag)
{
    S_D(CppTypeEntry);
    d->m_defaultConstructibleFlag = flag;
}

bool CppTypeEntry::isDefaultConstructible() const
{
    S_D(const CppTypeEntry);
    switch (d->m_defaultConstructibleFlag) {
    case TypeSystem::DefaultConstructibleFlag::Enabled:
        return true;
    case TypeSystem::DefaultConstructibleFlag::Disabled:
         return false;
    case TypeSystem::DefaultConstructibleFlag::Unspecified:
        break;
    }
    return d->m_defaultConstructibleDetected || !d->m_defaultConstructor.isEmpty();
}

void CppTypeEntry::setDefaultConstructibleDetected(bool c)
{
    S_D(CppTypeEntry);
    d->m_defaultConstructibleDetected = c;
}

TypeSystem::CopyableFlag CppTypeEntry::copyableFlag() const
{
    S_D(const CppTypeEntry);
    return d->m_copyableFlag;
}

void CppTypeEntry::setCopyableFlag(TypeSystem::CopyableFlag flag)
{
    S_D(CppTypeEntry);
    d->m_copyableFlag = flag;
}

bool CppTypeEntry::isCopyable() const
{
    S_D(const CppTypeEntry);
    switch (d->m_copyableFlag) {
    case TypeSystem::CopyableFlag::Enabled:
        return true;
    case TypeSystem::CopyableFlag::Disabled:
        return false;
    case TypeSystem::CopyableFlag::Unspecified:
        break;
    }
    return d->m_copyableDetected;
}

void CppTypeEntry::setCopyableDetected(bool c)
{
    S_D(CppTypeEntry);
    d->m_copyableDetected = c;
}

// Movable has no detection logic in the code model (yet). It is turned off for namespaces
TypeSystem::MovableFlag CppTypeEntry::movableFlag() const
{
    S_D(const CppTypeEntry);
    return d->m_movableFlag;
}

void CppTypeEntry::setMovableFlag(TypeSystem::MovableFlag flag)
{
    S_D(CppTypeEntry);
    d->m_movableFlag = flag;
}

bool CppTypeEntry::isMovable() const
{
    S_D(const CppTypeEntry);
    switch (d->m_movableFlag) {
    case TypeSystem::MovableFlag::Enabled:
        return true;
    case TypeSystem::MovableFlag::Disabled:
        return false;
    case TypeSystem::MovableFlag::Unspecified:
        break;
    }
    return d->m_movableDetected;
}

TypeSystem::DestructorType CppTypeEntry::destructorType() const
{
    S_D(const CppTypeEntry);
    switch (d->m_destructibleFlag) {
    case TypeSystem::DestructibleFlag::Enabled:
        return TypeSystem::DestructorType::PublicDestructor;
    case TypeSystem::DestructibleFlag::Disabled:
        return TypeSystem::DestructorType::ProtectedDestructor;
    case TypeSystem::DestructibleFlag::Unspecified:
        break;
    }
    return d->m_detectedDestructor;
}

TypeSystem::DestructibleFlag CppTypeEntry::destructibleFlag() const
{
    S_D(const CppTypeEntry);
    return d->m_destructibleFlag;
}

void CppTypeEntry::setDestructibleFlag(TypeSystem::DestructibleFlag flag)
{
    S_D(CppTypeEntry);
    d->m_destructibleFlag = flag;
}

void CppTypeEntry::setDetectedDestructorType(TypeSystem::DestructorType dt)
{
    S_D(CppTypeEntry);
    d->m_detectedDestructor = dt;
}

TypeSystem::QtMetaTypeRegistration CppTypeEntry::qtMetaTypeRegistration() const
{
    S_D(const CppTypeEntry);
    return d->m_qtMetaTypeRegistration;
}

void CppTypeEntry::setQtMetaTypeRegistration(TypeSystem::QtMetaTypeRegistration r)
{
    S_D(CppTypeEntry);
    d->m_qtMetaTypeRegistration = r;
}

TypeEntry *CppTypeEntry::clone() const
{
    S_D(const CppTypeEntry);
    return new CppTypeEntry(new CppTypeEntryPrivate(*d));
}

// ----------------- CustomTypeEntry
class CustomTypeEntryPrivate : public CppTypeEntryPrivate
{
public:
    using CppTypeEntryPrivate::CppTypeEntryPrivate;

    QString m_checkFunction;
};

CustomTypeEntry::CustomTypeEntry(const QString &entryName, const QVersionNumber &vr,
                                 const TypeEntryCPtr &parent) :
    CppTypeEntry(new CustomTypeEntryPrivate(entryName, CustomType, vr, parent))
{
}

CustomTypeEntry::CustomTypeEntry(CustomTypeEntryPrivate *d) :
    CppTypeEntry(d)
{
}

TypeEntry *CustomTypeEntry::clone() const
{
    S_D(const CustomTypeEntry);
    return new CustomTypeEntry(new CustomTypeEntryPrivate(*d));
}

bool CustomTypeEntry::hasCheckFunction() const
{
    S_D(const CustomTypeEntry);
    return !d->m_checkFunction.isEmpty();
}

QString CustomTypeEntry::checkFunction() const
{
    S_D(const CustomTypeEntry);
    return d->m_checkFunction;
}

void CustomTypeEntry::setCheckFunction(const QString &f)
{
    S_D(CustomTypeEntry);
    d->m_checkFunction = f;
}

// ----------------- PythonTypeEntry
class PythonTypeEntryPrivate : public CustomTypeEntryPrivate
{
public:
    using CustomTypeEntryPrivate::CustomTypeEntryPrivate;
    explicit PythonTypeEntryPrivate(const QString &entryName,
                                    const QString &checkFunction,
                                    TypeSystem::CPythonType type) :
        CustomTypeEntryPrivate(entryName, TypeEntry::PythonType, {}, {}),
        m_cPythonType(type)
    {
        m_checkFunction = checkFunction;
    }

    TypeSystem::CPythonType m_cPythonType;
};

PythonTypeEntry::PythonTypeEntry(const QString &entryName,
                                 const QString &checkFunction,
                                 TypeSystem::CPythonType type) :
    CustomTypeEntry(new PythonTypeEntryPrivate(entryName, checkFunction, type))
{
}

TypeEntry *PythonTypeEntry::clone() const
{
    S_D(const PythonTypeEntry);
    return new PythonTypeEntry(new PythonTypeEntryPrivate(*d));
}

TypeSystem::CPythonType PythonTypeEntry::cPythonType() const
{
    S_D(const PythonTypeEntry);
    return d->m_cPythonType;
}

PythonTypeEntry::PythonTypeEntry(CustomTypeEntryPrivate *d) :
    CustomTypeEntry(d)
{
}

// ----------------- TypeSystemTypeEntry
class TypeSystemTypeEntryPrivate : public TypeEntryPrivate
{
public:
    using TypeEntryPrivate::TypeEntryPrivate;

    CodeSnipList m_codeSnips;
    TypeSystem::SnakeCase m_snakeCase = TypeSystem::SnakeCase::Disabled;
    QString m_subModuleOf;
    QString m_namespaceBegin;
    QString m_namespaceEnd;
    QString m_docTargetLangPackage;
    TypeSystem::DocMode m_docMode = TypeSystem::DocMode::Nested;
};

TypeSystemTypeEntry::TypeSystemTypeEntry(const QString &entryName, const QVersionNumber &vr,
                                         const TypeEntryCPtr &parent) :
    TypeEntry(new TypeSystemTypeEntryPrivate(entryName, TypeSystemType, vr, parent))
{
}

TypeSystemTypeEntry::TypeSystemTypeEntry(TypeEntryPrivate *d) :
    TypeEntry(d)
{
}

TypeEntry *TypeSystemTypeEntry::clone() const
{
    S_D(const TypeSystemTypeEntry);
    return new TypeSystemTypeEntry(new TypeSystemTypeEntryPrivate(*d));
}

const CodeSnipList &TypeSystemTypeEntry::codeSnips() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_codeSnips;
}

CodeSnipList &TypeSystemTypeEntry::codeSnips()
{
    S_D(TypeSystemTypeEntry);
    return d->m_codeSnips;
}

void TypeSystemTypeEntry::addCodeSnip(const CodeSnip &codeSnip)
{
    S_D(TypeSystemTypeEntry);
    d->m_codeSnips.append(codeSnip);
}

QString TypeSystemTypeEntry::subModuleOf() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_subModuleOf;
}

void TypeSystemTypeEntry::setSubModule(const QString &s)
{
    S_D(TypeSystemTypeEntry);
    d->m_subModuleOf = s;
}

bool TypeSystemTypeEntry::hasDocTargetLangPackage() const
{
    S_D(const TypeSystemTypeEntry);
    return !d->m_docTargetLangPackage.isEmpty();
}

QString TypeSystemTypeEntry::docTargetLangPackage() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_docTargetLangPackage;
}

void TypeSystemTypeEntry::setDocTargetLangPackage(const QString &p)
{
    S_D(TypeSystemTypeEntry);
    d->m_docTargetLangPackage = p;
}

TypeSystem::DocMode TypeSystemTypeEntry::docMode() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_docMode;
}

void TypeSystemTypeEntry::setDocMode(TypeSystem::DocMode m)
{
    S_D(TypeSystemTypeEntry);
    d->m_docMode = m;
}

const QString &TypeSystemTypeEntry::namespaceBegin() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_namespaceBegin;
}

void TypeSystemTypeEntry::setNamespaceBegin(const QString &n)
{
    S_D(TypeSystemTypeEntry);
    d->m_namespaceBegin = n;
}

const QString &TypeSystemTypeEntry::namespaceEnd() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_namespaceEnd;
}

void TypeSystemTypeEntry::setNamespaceEnd(const QString &n)
{
    S_D(TypeSystemTypeEntry);
    d->m_namespaceEnd = n;
}

bool TypeSystemTypeEntry::usesGil() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_usesGil;
}

void TypeSystemTypeEntry::setUsesGil(bool g)
{
    S_D(TypeSystemTypeEntry);
    d->m_usesGil = g;
}

TypeSystem::SnakeCase TypeSystemTypeEntry::snakeCase() const
{
    S_D(const TypeSystemTypeEntry);
    return d->m_snakeCase;
}

void TypeSystemTypeEntry::setSnakeCase(TypeSystem::SnakeCase sc)
{
    S_D(TypeSystemTypeEntry);
    d->m_snakeCase = sc;
}

// ----------------- VoidTypeEntry
VoidTypeEntry::VoidTypeEntry() :
    CppTypeEntry(u"void"_s, VoidType, QVersionNumber(0, 0), nullptr)
{
}

VoidTypeEntry::VoidTypeEntry(CppTypeEntryPrivate *d) :
    CppTypeEntry(d)
{
}

TypeEntry *VoidTypeEntry::clone() const
{
    S_D(const CppTypeEntry);
    return new VoidTypeEntry(new CppTypeEntryPrivate(*d));
}

VarargsTypeEntry::VarargsTypeEntry() :
    TypeEntry(u"..."_s, VarargsType, QVersionNumber(0, 0), nullptr)
{
}

// ----------------- VarargsTypeEntry
TypeEntry *VarargsTypeEntry::clone() const
{
    return new VarargsTypeEntry(new TypeEntryPrivate(*d_func()));
}

VarargsTypeEntry::VarargsTypeEntry(TypeEntryPrivate *d) :
    TypeEntry(d)
{
}

// ----------------- TemplateArgumentEntry
class TemplateArgumentEntryPrivate : public TypeEntryPrivate
{
public:
    using TypeEntryPrivate::TypeEntryPrivate;

    int m_ordinal = 0;
};

TemplateArgumentEntry::TemplateArgumentEntry(const QString &entryName, const QVersionNumber &vr,
                                             const TypeEntryCPtr &parent) :
    TypeEntry(new TemplateArgumentEntryPrivate(entryName, TemplateArgumentType, vr, parent))
{
}

int TemplateArgumentEntry::ordinal() const
{
    S_D(const TemplateArgumentEntry);
    return d->m_ordinal;
}

void TemplateArgumentEntry::setOrdinal(int o)
{
    S_D(TemplateArgumentEntry);
    d->m_ordinal = o;
}

TypeEntry *TemplateArgumentEntry::clone() const
{
    S_D(const TemplateArgumentEntry);
    return new TemplateArgumentEntry(new TemplateArgumentEntryPrivate(*d));
}

TemplateArgumentEntry::TemplateArgumentEntry(TemplateArgumentEntryPrivate *d) :
    TypeEntry(d)
{
}

// ----------------- ArrayTypeEntry
class ArrayTypeEntryPrivate : public CppTypeEntryPrivate
{
public:
    explicit ArrayTypeEntryPrivate(const TypeEntryCPtr &nested_type, const QVersionNumber &vr,
                                   const TypeEntryCPtr &parent) :
        CppTypeEntryPrivate(u"Array"_s, TypeEntry::ArrayType, vr, parent),
        m_nestedType(nested_type)
    {
    }

    TypeEntryCPtr m_nestedType;
};

ArrayTypeEntry::ArrayTypeEntry(const TypeEntryCPtr &nested_type, const QVersionNumber &vr,
                               const TypeEntryCPtr &parent) :
    CppTypeEntry(new ArrayTypeEntryPrivate(nested_type, vr, parent))
{
    Q_ASSERT(nested_type);
}

void ArrayTypeEntry::setNestedTypeEntry(const TypeEntryPtr &nested)
{
    S_D(ArrayTypeEntry);
    d->m_nestedType = nested;
}

TypeEntryCPtr ArrayTypeEntry::nestedTypeEntry() const
{
    S_D(const ArrayTypeEntry);
    return d->m_nestedType;
}

QString ArrayTypeEntry::buildTargetLangName() const
{
    S_D(const ArrayTypeEntry);
    return d->m_nestedType->targetLangName() + u"[]"_s;
}

TypeEntry *ArrayTypeEntry::clone() const
{
    S_D(const ArrayTypeEntry);
    return new ArrayTypeEntry(new ArrayTypeEntryPrivate(*d));
}

ArrayTypeEntry::ArrayTypeEntry(ArrayTypeEntryPrivate *d) :
    CppTypeEntry(d)
{
}

// ----------------- PrimitiveTypeEntry
class PrimitiveTypeEntryPrivate : public CppTypeEntryPrivate
{
public:
    PrimitiveTypeEntryPrivate(const QString &entryName, const QVersionNumber &vr,
                              const TypeEntryCPtr &parent) :
        CppTypeEntryPrivate(entryName, TypeEntry::PrimitiveType, vr, parent),
        m_preferredTargetLangType(true)
    {
    }

    CustomConversionPtr m_customConversion;
    PrimitiveTypeEntryPtr m_referencedTypeEntry;
    uint m_preferredTargetLangType : 1;
};

PrimitiveTypeEntry::PrimitiveTypeEntry(const QString &entryName, const QVersionNumber &vr,
                                       const TypeEntryCPtr &parent) :
    CppTypeEntry(new PrimitiveTypeEntryPrivate(entryName, vr, parent))
{
}

PrimitiveTypeEntryPtr PrimitiveTypeEntry::referencedTypeEntry() const
{
    S_D(const PrimitiveTypeEntry);
    return d->m_referencedTypeEntry;
}

void PrimitiveTypeEntry::setReferencedTypeEntry(const PrimitiveTypeEntryPtr &referencedTypeEntry)
{
    S_D(PrimitiveTypeEntry);
    d->m_referencedTypeEntry = referencedTypeEntry;
}

PrimitiveTypeEntryCPtr basicReferencedTypeEntry(const PrimitiveTypeEntryCPtr &e)
{
    auto result = e;
    while (auto referenced = result->referencedTypeEntry())
        result = referenced;
    return result;
}

PrimitiveTypeEntryCPtr basicReferencedTypeEntry(const TypeEntryCPtr &e)
{
    Q_ASSERT(e->isPrimitive());
    return basicReferencedTypeEntry(std::static_pointer_cast<const PrimitiveTypeEntry>(e));
}

PrimitiveTypeEntryCPtr basicReferencedNonBuiltinTypeEntry(const PrimitiveTypeEntryCPtr &e)
{
    auto result = e;
    for (;  result->referencedTypeEntry() ; result = result->referencedTypeEntry()) {
        if (!result->isBuiltIn())
            break;
    }
    return result;
}

bool PrimitiveTypeEntry::referencesType() const
{
    S_D(const PrimitiveTypeEntry);
    return d->m_referencedTypeEntry != nullptr;
}

bool PrimitiveTypeEntry::preferredTargetLangType() const
{
    S_D(const PrimitiveTypeEntry);
    return d->m_preferredTargetLangType;
}

void PrimitiveTypeEntry::setPreferredTargetLangType(bool b)
{
    S_D(PrimitiveTypeEntry);
    d->m_preferredTargetLangType = b;
}

bool PrimitiveTypeEntry::hasCustomConversion() const
{
    S_D(const PrimitiveTypeEntry);
    return bool(d->m_customConversion);
}

void PrimitiveTypeEntry::setCustomConversion(const CustomConversionPtr &customConversion)
{
    S_D(PrimitiveTypeEntry);
    d->m_customConversion = customConversion;
}

CustomConversionPtr PrimitiveTypeEntry::customConversion() const
{
    S_D(const PrimitiveTypeEntry);
    return d->m_customConversion;
}

TypeEntry *PrimitiveTypeEntry::clone() const
{
    S_D(const PrimitiveTypeEntry);
    return new PrimitiveTypeEntry(new PrimitiveTypeEntryPrivate(*d));
}

PrimitiveTypeEntry::PrimitiveTypeEntry(PrimitiveTypeEntryPrivate *d)
    : CppTypeEntry(d)
{
}

// ----------------- ConfigurableTypeEntry

class ConfigurableTypeEntryPrivate : public CppTypeEntryPrivate
{
public:
    using CppTypeEntryPrivate::CppTypeEntryPrivate;

    QString m_configCondition;
};

ConfigurableTypeEntry::ConfigurableTypeEntry(const QString &entryName, Type t,
                                             const QVersionNumber &vr,
                                             const TypeEntryCPtr &parent) :
    CppTypeEntry(new ConfigurableTypeEntryPrivate(entryName, t, vr, parent))
{
}

ConfigurableTypeEntry::ConfigurableTypeEntry(ConfigurableTypeEntryPrivate *d) :
    CppTypeEntry(d)
{
}

TypeEntry *ConfigurableTypeEntry::clone() const
{
    S_D(const ConfigurableTypeEntry);
    return new ConfigurableTypeEntry(new ConfigurableTypeEntryPrivate(*d));
}

QString ConfigurableTypeEntry::configCondition() const
{
    S_D(const ConfigurableTypeEntry);
    return d->m_configCondition;
}

void ConfigurableTypeEntry::setConfigCondition(const QString &c)
{
    S_D(ConfigurableTypeEntry);
    d->m_configCondition = c;
    if (!d->m_configCondition.startsWith(u'#'))
        d->m_configCondition.prepend(u"#if ");
}

bool ConfigurableTypeEntry::hasConfigCondition() const
{
    S_D(const ConfigurableTypeEntry);
    return !d->m_configCondition.isEmpty();
}

// ----------------- EnumTypeEntry
class EnumTypeEntryPrivate : public ConfigurableTypeEntryPrivate
{
public:
    using ConfigurableTypeEntryPrivate::ConfigurableTypeEntryPrivate;

    EnumValueTypeEntryCPtr m_nullValue;
    QStringList m_rejectedEnums;
    FlagsTypeEntryPtr m_flags;
    QString m_cppType;
    QString m_docFile;
    std::weak_ptr<const EnumTypeEntry> m_aliasTypeEntry;
    TypeSystem::PythonEnumType m_pythonEnumType = TypeSystem::PythonEnumType::Unspecified;
    EnumTypeEntry::AliasMode m_aliasMode = EnumTypeEntry::AliasMode::NoAlias;
    bool m_forceUnsigned = false;
};

EnumTypeEntry::EnumTypeEntry(const QString &entryName,
                             const QVersionNumber &vr,
                             const TypeEntryCPtr &parent) :
    ConfigurableTypeEntry(new EnumTypeEntryPrivate(entryName, EnumType, vr, parent))
{
}

TypeSystem::PythonEnumType EnumTypeEntry::pythonEnumType() const
{
    S_D(const EnumTypeEntry);
    return d->m_pythonEnumType;
}

void EnumTypeEntry::setPythonEnumType(TypeSystem::PythonEnumType t)
{
    S_D(EnumTypeEntry);
    d->m_pythonEnumType = t;
}

QString EnumTypeEntry::targetLangQualifier() const
{
    QString q = qualifier();
    if (!q.isEmpty()) {
        if (auto te = TypeDatabase::instance()->findType(q))
            return te->targetLangName();
    }
    return q;
}

QString EnumTypeEntry::qualifier() const
{
    auto parentEntry = parent();
    return parentEntry && parentEntry->type() != TypeEntry::TypeSystemType ?
        parentEntry->name() : QString();
}

EnumValueTypeEntryCPtr EnumTypeEntry::nullValue() const
{
    S_D(const EnumTypeEntry);
    return d->m_nullValue;
}

void EnumTypeEntry::setNullValue(const EnumValueTypeEntryCPtr &n)
{
    S_D(EnumTypeEntry);
    d->m_nullValue = n;
}

void EnumTypeEntry::setFlags(const FlagsTypeEntryPtr &flags)
{
    S_D(EnumTypeEntry);
    d->m_flags = flags;
}

FlagsTypeEntryPtr EnumTypeEntry::flags() const
{
    S_D(const EnumTypeEntry);
    return d->m_flags;
}

QString EnumTypeEntry::cppType() const
{
    S_D(const EnumTypeEntry);
    return d->m_cppType;
}

void EnumTypeEntry::setCppType(const QString &t)
{
    S_D(EnumTypeEntry);
    d->m_cppType = t;
}

void EnumTypeEntry::setForceUnsigned(bool f)
{
    S_D(EnumTypeEntry);
    d->m_forceUnsigned = f;
}

bool EnumTypeEntry::forceUnsigned() const
{
    S_D(const EnumTypeEntry);
    return d->m_forceUnsigned;
}

bool EnumTypeEntry::isEnumValueRejected(const QString &name) const
{
    S_D(const EnumTypeEntry);
    return d->m_rejectedEnums.contains(name);
}

void EnumTypeEntry::addEnumValueRejection(const QString &name)
{
    S_D(EnumTypeEntry);
    d->m_rejectedEnums << name;
}

QStringList EnumTypeEntry::enumValueRejections() const
{
    S_D(const EnumTypeEntry);
    return d->m_rejectedEnums;
}

QString EnumTypeEntry::docFile() const
{
    S_D(const EnumTypeEntry);
    return d->m_docFile;
}

void EnumTypeEntry::setDocFile(const QString &df)
{
    S_D(EnumTypeEntry);
    d->m_docFile = df;
}

EnumTypeEntry::AliasMode EnumTypeEntry::aliasMode() const
{
    S_D(const EnumTypeEntry);
    return d->m_aliasMode;
}

void EnumTypeEntry::setAliasMode(AliasMode am)
{
    S_D(EnumTypeEntry);
    d->m_aliasMode = am;
}

EnumTypeEntryCPtr EnumTypeEntry::aliasTypeEntry() const
{
    S_D(const EnumTypeEntry);
    return d->m_aliasTypeEntry.lock();
}

void EnumTypeEntry::setAliasTypeEntry(const EnumTypeEntryCPtr &entry)
{
    S_D(EnumTypeEntry);
    d->m_aliasTypeEntry = entry;
}

TypeEntry *EnumTypeEntry::clone() const
{
    S_D(const EnumTypeEntry);
    return new EnumTypeEntry(new EnumTypeEntryPrivate(*d));
}

EnumTypeEntry::EnumTypeEntry(EnumTypeEntryPrivate *d) :
    ConfigurableTypeEntry(d)
{
}

// ----------------- EnumValueTypeEntryPrivate
class EnumValueTypeEntryPrivate : public TypeEntryPrivate
{
public:
    EnumValueTypeEntryPrivate(const QString &name, const QString &value,
                              const EnumTypeEntryCPtr &enclosingEnum,
                              bool isScopedEnum,
                              const QVersionNumber &vr) :
        TypeEntryPrivate(name, TypeEntry::EnumValue, vr,
                         isScopedEnum ? enclosingEnum : enclosingEnum->parent()),
               m_value(value),
               m_enclosingEnum(enclosingEnum)
    {
    }

    QString m_value;
    EnumTypeEntryCPtr m_enclosingEnum;
};

EnumValueTypeEntry::EnumValueTypeEntry(const QString &name, const QString &value,
                                       const EnumTypeEntryCPtr &enclosingEnum,
                                       bool isScopedEnum,
                                       const QVersionNumber &vr) :
    TypeEntry(new EnumValueTypeEntryPrivate(name, value, enclosingEnum, isScopedEnum, vr))
{
}

QString EnumValueTypeEntry::value() const
{
    S_D(const EnumValueTypeEntry);
    return d->m_value;
}

EnumTypeEntryCPtr EnumValueTypeEntry::enclosingEnum() const
{
    S_D(const EnumValueTypeEntry);
    return d->m_enclosingEnum;
}

TypeEntry *EnumValueTypeEntry::clone() const
{
    S_D(const EnumValueTypeEntry);
    return new EnumValueTypeEntry(new EnumValueTypeEntryPrivate(*d));
}

EnumValueTypeEntry::EnumValueTypeEntry(EnumValueTypeEntryPrivate *d) :
    TypeEntry(d)
{
}

// ----------------- FlagsTypeEntry
class FlagsTypeEntryPrivate : public CppTypeEntryPrivate
{
public:
    using CppTypeEntryPrivate::CppTypeEntryPrivate;

    QString m_originalName;
    QString m_flagsName;
    EnumTypeEntryPtr m_enum;
};

FlagsTypeEntry::FlagsTypeEntry(const QString &entryName, const QVersionNumber &vr,
                               const TypeEntryCPtr &parent) :
    CppTypeEntry(new FlagsTypeEntryPrivate(entryName, FlagsType, vr, parent))
{
}

QString FlagsTypeEntry::buildTargetLangName() const
{
    S_D(const FlagsTypeEntry);
    QString on = d->m_originalName;
    on.replace(u"::"_s, u"."_s);
    return on;
}

FlagsTypeEntry::FlagsTypeEntry(FlagsTypeEntryPrivate *d) :
    CppTypeEntry(d)
{
}

QString FlagsTypeEntry::originalName() const
{
    S_D(const FlagsTypeEntry);
    return d->m_originalName;
}

void FlagsTypeEntry::setOriginalName(const QString &s)
{
    S_D(FlagsTypeEntry);
    d->m_originalName = s;
}

QString FlagsTypeEntry::flagsName() const
{
    S_D(const FlagsTypeEntry);
    return d->m_flagsName;
}

void FlagsTypeEntry::setFlagsName(const QString &name)
{
    S_D(FlagsTypeEntry);
    d->m_flagsName = name;
}

EnumTypeEntryPtr FlagsTypeEntry::originator() const
{
    S_D(const FlagsTypeEntry);
    return d->m_enum;
}

void FlagsTypeEntry::setOriginator(const EnumTypeEntryPtr &e)
{
    S_D(FlagsTypeEntry);
    d->m_enum = e;
}

TypeEntry *FlagsTypeEntry::clone() const
{
    S_D(const FlagsTypeEntry);
    return new FlagsTypeEntry(new FlagsTypeEntryPrivate(*d));
}

// ----------------- ConstantValueTypeEntry
ConstantValueTypeEntry::ConstantValueTypeEntry(const QString& name,
                                               const TypeEntryCPtr &parent) :
    TypeEntry(name, ConstantValueType, QVersionNumber(0, 0), parent)
{
}

ConstantValueTypeEntry::ConstantValueTypeEntry(TypeEntryPrivate *d) :
    TypeEntry(d)
{
}

// ----------------- ComplexTypeEntry
class ComplexTypeEntryPrivate : public ConfigurableTypeEntryPrivate
{
public:
    ComplexTypeEntryPrivate(const QString &entryName, TypeEntry::Type t,
                            const QVersionNumber &vr,
                            const TypeEntryCPtr &parent) :
        ConfigurableTypeEntryPrivate(entryName, t, vr, parent),
        m_qualifiedCppName(buildName(entryName, parent)),
        m_polymorphicBase(false),
        m_genericClass(false)
    {
    }

    AddedFunctionList m_addedFunctions;
    FunctionModificationList m_functionMods;
    CodeSnipList m_codeSnips;
    DocModificationList m_docModifications;
    DocModificationList m_functionDocModifications;
    IncludeList m_argumentIncludes;
    QSet<QString> m_generateFunctions;
    FieldModificationList m_fieldMods;
    QList<TypeSystemProperty> m_properties;
    QList<TypeSystemPyMethodDefEntry> m_PyMethodDefEntrys;
    QString m_defaultSuperclass;
    QString m_qualifiedCppName;
    QString m_docFile;

    TypeSystem::DeletionMode m_deletionMode = TypeSystem::DeletionMode::Default;

    uint m_polymorphicBase : 1;
    uint m_genericClass : 1;

    QString m_polymorphicIdValue;
    QString m_polymorphicNameFunction;
    QString m_targetType;
    ComplexTypeEntry::TypeFlags m_typeFlags;
    QString m_hashFunction;

    ComplexTypeEntryCPtr m_baseContainerType;
    // For class functions
    TypeSystem::ExceptionHandling m_exceptionHandling = TypeSystem::ExceptionHandling::Unspecified;
    TypeSystem::AllowThread m_allowThread = TypeSystem::AllowThread::Unspecified;
    TypeSystem::SnakeCase m_snakeCase = TypeSystem::SnakeCase::Unspecified;
    TypeSystem::BoolCast m_operatorBoolMode = TypeSystem::BoolCast::Unspecified;
    TypeSystem::BoolCast m_isNullMode = TypeSystem::BoolCast::Unspecified;
    TypeSystem::QtMetaTypeRegistration m_qtMetaTypeRegistration =
        TypeSystem::QtMetaTypeRegistration::Unspecified;
};

ComplexTypeEntry::ComplexTypeEntry(const QString &entryName, TypeEntry::Type t,
                                   const QVersionNumber &vr,
                                   const TypeEntryCPtr &parent) :
    ConfigurableTypeEntry(new ComplexTypeEntryPrivate(entryName, t, vr, parent))
{
}

bool ComplexTypeEntry::isComplex() const
{
    return true;
}

void ComplexTypeEntry::setTypeFlags(TypeFlags flags)
{
    S_D(ComplexTypeEntry);
    d->m_typeFlags = flags;
}

TypeSystem::BoolCast ComplexTypeEntry::operatorBoolMode() const
{
    S_D(const ComplexTypeEntry);
    return d->m_operatorBoolMode;
}

void ComplexTypeEntry::setOperatorBoolMode(TypeSystem::BoolCast b)
{
    S_D(ComplexTypeEntry);
    d->m_operatorBoolMode = b;
}

TypeSystem::BoolCast ComplexTypeEntry::isNullMode() const
{
    S_D(const ComplexTypeEntry);
    return d->m_isNullMode;
}

void ComplexTypeEntry::setIsNullMode(TypeSystem::BoolCast b)
{
    S_D(ComplexTypeEntry);
    d->m_isNullMode = b;
}

ComplexTypeEntry::TypeFlags ComplexTypeEntry::typeFlags() const
{
    S_D(const ComplexTypeEntry);
    return d->m_typeFlags;
}

FunctionModificationList ComplexTypeEntry::functionModifications() const
{
    S_D(const ComplexTypeEntry);
    return d->m_functionMods;
}

void ComplexTypeEntry::setFunctionModifications(const FunctionModificationList &functionModifications)
{
    S_D(ComplexTypeEntry);
    d->m_functionMods = functionModifications;
}

void ComplexTypeEntry::addFunctionModification(const FunctionModification &functionModification)
{
    S_D(ComplexTypeEntry);
    d->m_functionMods << functionModification;
}

FunctionModificationList
    ComplexTypeEntry::functionModifications(const QStringList &signatures) const
{
    S_D(const ComplexTypeEntry);
    FunctionModificationList lst;
    for (const auto &mod : std::as_const(d->m_functionMods)) {
        if (mod.matches(signatures))
            lst << mod;
    }
    return lst;
}

const CodeSnipList &ComplexTypeEntry::codeSnips() const
{
    S_D(const ComplexTypeEntry);
    return d->m_codeSnips;
}

CodeSnipList &ComplexTypeEntry::codeSnips()
{
    S_D(ComplexTypeEntry);
    return d->m_codeSnips;
}

void ComplexTypeEntry::setCodeSnips(const CodeSnipList &codeSnips)
{
    S_D(ComplexTypeEntry);
    d->m_codeSnips = codeSnips;
}

void ComplexTypeEntry::addCodeSnip(const CodeSnip &codeSnip)
{
    S_D(ComplexTypeEntry);
    d->m_codeSnips << codeSnip;
}

void ComplexTypeEntry::setDocModification(const DocModificationList &docMods)
{
    S_D(ComplexTypeEntry);
    for (const auto &m : docMods) {
        if (m.signature().isEmpty())
            d->m_docModifications << m;
        else
            d->m_functionDocModifications << m;
    }
}

DocModificationList ComplexTypeEntry::docModifications() const
{
    S_D(const ComplexTypeEntry);
    return d->m_docModifications;
}

DocModificationList ComplexTypeEntry::functionDocModifications() const
{
    S_D(const ComplexTypeEntry);
    return d->m_functionDocModifications;
}

const IncludeList &ComplexTypeEntry::argumentIncludes() const
{
    S_D(const ComplexTypeEntry);
    return d->m_argumentIncludes;
}

void ComplexTypeEntry::addArgumentInclude(const Include &newInclude)
{
    S_D(ComplexTypeEntry);
    IncludeGroup::appendInclude(newInclude, &d->m_argumentIncludes);
}

AddedFunctionList ComplexTypeEntry::addedFunctions() const
{
    S_D(const ComplexTypeEntry);
    return d->m_addedFunctions;
}

void ComplexTypeEntry::setAddedFunctions(const AddedFunctionList &addedFunctions)
{
    S_D(ComplexTypeEntry);
    d->m_addedFunctions = addedFunctions;
}

void ComplexTypeEntry::addNewFunction(const AddedFunctionPtr &addedFunction)
{
    S_D(ComplexTypeEntry);
    d->m_addedFunctions << addedFunction;
}

const QList<TypeSystemPyMethodDefEntry> &ComplexTypeEntry::addedPyMethodDefEntrys() const
{
    S_D(const ComplexTypeEntry);
    return d->m_PyMethodDefEntrys;
}

void ComplexTypeEntry::addPyMethodDef(const TypeSystemPyMethodDefEntry &p)
{
    S_D(ComplexTypeEntry);
    d->m_PyMethodDefEntrys.append(p);
}

const QSet<QString> &ComplexTypeEntry::generateFunctions() const
{
    S_D(const ComplexTypeEntry);
    return d->m_generateFunctions;
}

void ComplexTypeEntry::setGenerateFunctions(const QSet<QString> &f)
{
    S_D(ComplexTypeEntry);
    d->m_generateFunctions = f;
}

void ComplexTypeEntry::setFieldModifications(const FieldModificationList &mods)
{
    S_D(ComplexTypeEntry);
    d->m_fieldMods = mods;
}

FieldModificationList ComplexTypeEntry::fieldModifications() const
{
    S_D(const ComplexTypeEntry);
    return d->m_fieldMods;
}

const QList<TypeSystemProperty> &ComplexTypeEntry::properties() const
{
    S_D(const ComplexTypeEntry);
    return d->m_properties;
}

void ComplexTypeEntry::addProperty(const TypeSystemProperty &p)
{
    S_D(ComplexTypeEntry);
    d->m_properties.append(p);
}

QString ComplexTypeEntry::defaultSuperclass() const
{
    S_D(const ComplexTypeEntry);
    return d->m_defaultSuperclass;
}

void ComplexTypeEntry::setDefaultSuperclass(const QString &sc)
{
    S_D(ComplexTypeEntry);
    d->m_defaultSuperclass = sc;
}

QString ComplexTypeEntry::qualifiedCppName() const
{
    S_D(const ComplexTypeEntry);
    return d->m_qualifiedCppName;
}

void ComplexTypeEntry::setIsPolymorphicBase(bool on)
{
    S_D(ComplexTypeEntry);
    d->m_polymorphicBase = on;
}

bool ComplexTypeEntry::isPolymorphicBase() const
{
    S_D(const ComplexTypeEntry);
    return d->m_polymorphicBase;
}

void ComplexTypeEntry::setPolymorphicIdValue(const QString &value)
{
    S_D(ComplexTypeEntry);
    d->m_polymorphicIdValue = value;
}

QString ComplexTypeEntry::polymorphicIdValue() const
{
    S_D(const ComplexTypeEntry);
    return d->m_polymorphicIdValue;
}

QString ComplexTypeEntry::polymorphicNameFunction() const
{
    S_D(const ComplexTypeEntry);
    return d->m_polymorphicNameFunction;
}

void ComplexTypeEntry::setPolymorphicNameFunction(const QString &n)
{
    S_D(ComplexTypeEntry);
    d->m_polymorphicNameFunction = n;
}

QString ComplexTypeEntry::targetType() const
{
    S_D(const ComplexTypeEntry);
    return d->m_targetType;
}

void ComplexTypeEntry::setTargetType(const QString &code)
{
    S_D(ComplexTypeEntry);
    d->m_targetType = code;
}

bool ComplexTypeEntry::isGenericClass() const
{
    S_D(const ComplexTypeEntry);
    return d->m_genericClass;
}

void ComplexTypeEntry::setGenericClass(bool isGeneric)
{
    S_D(ComplexTypeEntry);
    d->m_genericClass = isGeneric;
}

TypeSystem::DeletionMode ComplexTypeEntry::deletionMode() const
{
    S_D(const ComplexTypeEntry);
    return d->m_deletionMode;
}

void ComplexTypeEntry::setDeletionMode(TypeSystem::DeletionMode dm)
{
    S_D(ComplexTypeEntry);
    d->m_deletionMode = dm;
}

QString ComplexTypeEntry::hashFunction() const
{
    S_D(const ComplexTypeEntry);
    return d->m_hashFunction;
}

void ComplexTypeEntry::setHashFunction(const QString &hashFunction)
{
    S_D(ComplexTypeEntry);
    d->m_hashFunction = hashFunction;
}

void ComplexTypeEntry::setBaseContainerType(const ComplexTypeEntryCPtr &baseContainer)
{
    S_D(ComplexTypeEntry);
    d->m_baseContainerType = baseContainer;
}

ComplexTypeEntryCPtr ComplexTypeEntry::baseContainerType() const
{
    S_D(const ComplexTypeEntry);
    return d->m_baseContainerType;
}

TypeSystem::ExceptionHandling ComplexTypeEntry::exceptionHandling() const
{
    S_D(const ComplexTypeEntry);
    return d->m_exceptionHandling;
}

void ComplexTypeEntry::setExceptionHandling(TypeSystem::ExceptionHandling e)
{
    S_D(ComplexTypeEntry);
    d->m_exceptionHandling = e;
}

TypeSystem::AllowThread ComplexTypeEntry::allowThread() const
{
    S_D(const ComplexTypeEntry);
    return d->m_allowThread;
}

void ComplexTypeEntry::setAllowThread(TypeSystem::AllowThread allowThread)
{
    S_D(ComplexTypeEntry);
    d->m_allowThread = allowThread;
}

TypeSystem::SnakeCase ComplexTypeEntry::snakeCase() const
{
    S_D(const ComplexTypeEntry);
    return d->m_snakeCase;
}

void ComplexTypeEntry::setSnakeCase(TypeSystem::SnakeCase sc)
{
    S_D(ComplexTypeEntry);
    d->m_snakeCase = sc;
}

QString ComplexTypeEntry::docFile() const
{
    S_D(const ComplexTypeEntry);
    return d->m_docFile;
}

void ComplexTypeEntry::setDocFile(const QString &docFile)
{
    S_D(ComplexTypeEntry);
    d->m_docFile = docFile;
}

// FIXME PYSIDE 7: Remove this and make "true" the default
static bool parentManagementEnabled = false;

bool ComplexTypeEntry::isParentManagementEnabled()
{
    return parentManagementEnabled;
}

void ComplexTypeEntry::setParentManagementEnabled(bool e)
{
    parentManagementEnabled = e;
}

TypeEntry *ComplexTypeEntry::clone() const
{
    S_D(const ComplexTypeEntry);
    return new ComplexTypeEntry(new ComplexTypeEntryPrivate(*d));
}

// Take over parameters relevant for typedefs
void ComplexTypeEntry::useAsTypedef(const ComplexTypeEntryCPtr &source)
{
    S_D(ComplexTypeEntry);
    TypeEntry::useAsTypedef(source);
    d->m_qualifiedCppName = source->qualifiedCppName();
    d->m_targetType = source->targetType();
    d->m_typeFlags.setFlag(ComplexTypeEntry::Typedef);
}

ComplexTypeEntry::ComplexTypeEntry(ComplexTypeEntryPrivate *d) :
    ConfigurableTypeEntry(d)
{
}

TypeEntry *ConstantValueTypeEntry::clone() const
{
    return new ConstantValueTypeEntry(new TypeEntryPrivate(*d_func()));
}


// ----------------- TypedefEntry
/* A typedef entry allows for specifying template specializations in the
 * typesystem XML file. */
class TypedefEntryPrivate : public ComplexTypeEntryPrivate
{
public:
    TypedefEntryPrivate(const QString &entryName,
                        const QString &sourceType,
                        const QVersionNumber &vr,
                        const TypeEntryCPtr &parent) :
        ComplexTypeEntryPrivate(entryName, TypeEntry::TypedefType,
                                vr, parent),
        m_sourceType(sourceType)
    {
    }

    QString m_sourceType;
    ComplexTypeEntryCPtr m_source;
    ComplexTypeEntryPtr m_target;
};

TypedefEntry::TypedefEntry(const QString &entryName, const QString &sourceType,
                           const QVersionNumber &vr, const TypeEntryCPtr &parent) :
    ComplexTypeEntry(new TypedefEntryPrivate(entryName, sourceType, vr, parent))
{
}

QString TypedefEntry::sourceType() const
{
    S_D(const TypedefEntry);
    return d->m_sourceType;
}

void TypedefEntry::setSourceType(const QString &s)
{
    S_D(TypedefEntry);
    d->m_sourceType =s;
}

TypeEntry *TypedefEntry::clone() const
{
    S_D(const TypedefEntry);
    return new TypedefEntry(new TypedefEntryPrivate(*d));
}

ComplexTypeEntryCPtr TypedefEntry::source() const
{
    S_D(const TypedefEntry);
    return d->m_source;
}

void TypedefEntry::setSource(const ComplexTypeEntryCPtr &source)
{
    S_D(TypedefEntry);
    d->m_source = source;
}

ComplexTypeEntryPtr TypedefEntry::target() const
{
    S_D(const TypedefEntry);
    return d->m_target;
}

void TypedefEntry::setTarget(const ComplexTypeEntryPtr &target)
{
    S_D(TypedefEntry);
    d->m_target = target;
}

TypedefEntry::TypedefEntry(TypedefEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

// ----------------- ContainerTypeEntry
class ContainerTypeEntryPrivate : public ComplexTypeEntryPrivate
{
public:
    ContainerTypeEntryPrivate(const QString &entryName,
                              ContainerTypeEntry::ContainerKind containerKind,
                              const QVersionNumber &vr,
                              const TypeEntryCPtr &parent) :
        ComplexTypeEntryPrivate(entryName, TypeEntry::ContainerType, vr, parent),
        m_containerKind(containerKind)
    {
    }

    OpaqueContainers::const_iterator findOpaqueContainer(const QStringList &instantiations) const
    {
        return std::find_if(m_opaqueContainers.cbegin(), m_opaqueContainers.cend(),
                            [&instantiations](const OpaqueContainer &r) {
                                return r.instantiations == instantiations;
                            });
    }

    OpaqueContainers m_opaqueContainers;
    CustomConversionPtr m_customConversion;
    ContainerTypeEntry::ContainerKind m_containerKind;
};

QString OpaqueContainer::templateParameters() const
{
    QString result;
    result += u'<';
    for (qsizetype i = 0, size = instantiations.size(); i < size; ++i) {
        if (i)
            result += u',';
        result += instantiations.at(i);
    }
    result += u'>';
    return result;
}

ContainerTypeEntry::ContainerTypeEntry(const QString &entryName, ContainerKind containerKind,
                                       const QVersionNumber &vr,
                                       const TypeEntryCPtr &parent) :
    ComplexTypeEntry(new ContainerTypeEntryPrivate(entryName, containerKind, vr, parent))
{
    setCodeGeneration(GenerateForSubclass);
}

ContainerTypeEntry::ContainerKind ContainerTypeEntry::containerKind() const
{
    S_D(const ContainerTypeEntry);
    return d->m_containerKind;
}

qsizetype ContainerTypeEntry::templateParameterCount() const
{
    S_D(const ContainerTypeEntry);
    qsizetype result = 1;
    switch (d->m_containerKind) {
    case MapContainer:
    case MultiMapContainer:
    case PairContainer:
    case SpanContainer:
        result = 2;
        break;
    case ListContainer:
    case SetContainer:
        break;
    }
    return result;
}

const OpaqueContainers &ContainerTypeEntry::opaqueContainers() const
{
    S_D(const ContainerTypeEntry);
    return d->m_opaqueContainers;
}

void ContainerTypeEntry::appendOpaqueContainers(const OpaqueContainers &l)
{
    S_D(ContainerTypeEntry);
    d->m_opaqueContainers.append(l);
}

bool ContainerTypeEntry::generateOpaqueContainer(const QStringList &instantiations) const
{
    S_D(const ContainerTypeEntry);
    return d->findOpaqueContainer(instantiations) != d->m_opaqueContainers.cend();
}

QString ContainerTypeEntry::opaqueContainerName(const QStringList &instantiations) const
{
    S_D(const ContainerTypeEntry);
    const auto it = d->findOpaqueContainer(instantiations);
    return it != d->m_opaqueContainers.cend() ? it->name : QString{};
}

bool ContainerTypeEntry::hasCustomConversion() const
{
    S_D(const ContainerTypeEntry);
    return bool(d->m_customConversion);
}

void ContainerTypeEntry::setCustomConversion(const CustomConversionPtr &customConversion)
{
    S_D(ContainerTypeEntry);
    d->m_customConversion = customConversion;
}

CustomConversionPtr ContainerTypeEntry::customConversion() const
{
    S_D(const ContainerTypeEntry);
    return d->m_customConversion;
}

TypeEntry *ContainerTypeEntry::clone() const
{
    S_D(const ContainerTypeEntry);
    return new ContainerTypeEntry(new ContainerTypeEntryPrivate(*d));
}

ContainerTypeEntry::ContainerTypeEntry(ContainerTypeEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

// ----------------- SmartPointerTypeEntry
class SmartPointerTypeEntryPrivate : public ComplexTypeEntryPrivate
{
public:
    SmartPointerTypeEntryPrivate(const QString &entryName,
                                 const QString &getterName,
                                 TypeSystem::SmartPointerType type,
                                 const QString &refCountMethodName,
                                 const QVersionNumber &vr, const TypeEntryCPtr &parent) :
        ComplexTypeEntryPrivate(entryName, TypeEntry::SmartPointerType, vr, parent),
        m_getterName(getterName),
        m_refCountMethodName(refCountMethodName),
        m_smartPointerType(type)
    {
    }

    qsizetype instantiationIndex(const TypeEntryCPtr &t) const;

    QString m_getterName;
    QString m_refCountMethodName;
    QString m_valueCheckMethod;
    QString m_nullCheckMethod;
    QString m_resetMethod;
    SmartPointerTypeEntry::Instantiations m_instantiations;
    TypeEntryCList m_excludedInstantiations;
    CustomConversionPtr m_customConversion;
    TypeSystem::SmartPointerType m_smartPointerType;
    TypeSystem::SmartPointerToPythonConversion m_toPythonConversion =
        TypeSystem::SmartPointerToPythonConversion::Default;
};

qsizetype SmartPointerTypeEntryPrivate::instantiationIndex(const TypeEntryCPtr &t) const
{
    for (qsizetype i = 0, size = m_instantiations.size(); i < size; ++i) {
        if (m_instantiations.at(i).typeEntry == t)
            return i;
    }
    return -1;
}

SmartPointerTypeEntry::SmartPointerTypeEntry(const QString &entryName,
                                             const QString &getterName,
                                             TypeSystem::SmartPointerType smartPointerType,
                                             const QString &refCountMethodName,
                                             const QVersionNumber &vr,
                                             const TypeEntryCPtr &parent) :
    ComplexTypeEntry(new SmartPointerTypeEntryPrivate(entryName, getterName, smartPointerType,
                                                      refCountMethodName, vr, parent))
{
    S_D(SmartPointerTypeEntry);
    // Pre-set detection values which is not done by the code model since
    // the clang parser might not see all system library includes.
    d->m_defaultConstructibleDetected = smartPointerType != TypeSystem::SmartPointerType::Handle;
    d->m_copyableDetected = smartPointerType != TypeSystem::SmartPointerType::Unique;
}

TypeSystem::SmartPointerType SmartPointerTypeEntry::smartPointerType() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_smartPointerType;
}

QString SmartPointerTypeEntry::getter() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_getterName;
}

QString SmartPointerTypeEntry::refCountMethodName() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_refCountMethodName;
}

QString SmartPointerTypeEntry::valueCheckMethod() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_valueCheckMethod;
}

void SmartPointerTypeEntry::setValueCheckMethod(const QString &m)
{
    S_D(SmartPointerTypeEntry);
    d->m_valueCheckMethod = m;
}

QString SmartPointerTypeEntry::nullCheckMethod() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_nullCheckMethod;
}

void SmartPointerTypeEntry::setNullCheckMethod(const QString &f)
{
    S_D(SmartPointerTypeEntry);
    d->m_nullCheckMethod = f;
}

QString SmartPointerTypeEntry::resetMethod() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_resetMethod;
}

void SmartPointerTypeEntry::setResetMethod(const QString &f)
{
    S_D(SmartPointerTypeEntry);
    d->m_resetMethod = f;
}

TypeEntry *SmartPointerTypeEntry::clone() const
{
    S_D(const SmartPointerTypeEntry);
    return new SmartPointerTypeEntry(new SmartPointerTypeEntryPrivate(*d));
}

const SmartPointerTypeEntry::Instantiations &SmartPointerTypeEntry::instantiations() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_instantiations;
}

void SmartPointerTypeEntry::setInstantiations(const Instantiations &i)
{
    S_D(SmartPointerTypeEntry);
    d->m_instantiations = i;
}

void SmartPointerTypeEntry::setExcludedInstantiations(const TypeEntryCList  &ex)
{
    S_D(SmartPointerTypeEntry);
    d->m_excludedInstantiations = ex;
}

const TypeEntryCList &SmartPointerTypeEntry::excludedInstantiations() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_excludedInstantiations;
}

TypeSystem::SmartPointerToPythonConversion SmartPointerTypeEntry::toPythonConversion() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_toPythonConversion;
}

void  SmartPointerTypeEntry::setToPythonConversion(TypeSystem::SmartPointerToPythonConversion c)
{
    S_D(SmartPointerTypeEntry);
    d->m_toPythonConversion = c;
}

SmartPointerTypeEntry::SmartPointerTypeEntry(SmartPointerTypeEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

bool SmartPointerTypeEntry::matchesInstantiation(const TypeEntryCPtr &e) const
{
    S_D(const SmartPointerTypeEntry);
    // No instantiations specified, or match
    return !d->m_excludedInstantiations.contains(e)
        && (d->m_instantiations.isEmpty() || d->instantiationIndex(e) != -1);
}

static QString fixSmartPointerName(QString name)
{
    name.replace(u"::"_s, u"_"_s);
    name.replace(u'<', u'_');
    name.remove(u'>');
    name.remove(u' ');
    return name;
}

QString SmartPointerTypeEntry::getTargetName(const AbstractMetaType &metaType) const
{
    S_D(const SmartPointerTypeEntry);
    auto instantiatedTe = metaType.instantiations().constFirst().typeEntry();
    const auto index = d->instantiationIndex(instantiatedTe);
    if (index != -1 && !d->m_instantiations.at(index).name.isEmpty())
        return d->m_instantiations.at(index).name;

    QString name = metaType.cppSignature();
    const auto templatePos = name.indexOf(u'<');
    if (templatePos != -1) { // "std::shared_ptr<A::B>" -> "shared_ptr<A::B>"
        const auto colonPos = name.lastIndexOf(u"::"_s, templatePos);
        if (colonPos != -1)
            name.remove(0, colonPos + 2);
    }
    return fixSmartPointerName(name);
}

bool SmartPointerTypeEntry::hasCustomConversion() const
{
    S_D(const SmartPointerTypeEntry);
    return bool(d->m_customConversion);
}

void SmartPointerTypeEntry::setCustomConversion(const CustomConversionPtr &customConversion)
{
    S_D(SmartPointerTypeEntry);
    d->m_customConversion = customConversion;
}

CustomConversionPtr SmartPointerTypeEntry::customConversion() const
{
    S_D(const SmartPointerTypeEntry);
    return d->m_customConversion;
}

// ----------------- NamespaceTypeEntry
class NamespaceTypeEntryPrivate : public ComplexTypeEntryPrivate
{
public:
    using ComplexTypeEntryPrivate::ComplexTypeEntryPrivate;

    QRegularExpression m_filePattern;
    NamespaceTypeEntryCPtr m_extends;
    TypeSystem::Visibility m_visibility = TypeSystem::Visibility::Auto;
    bool m_hasPattern = false;
    bool m_inlineNamespace = false;
    bool m_generateUsing = true; // Whether to generate "using namespace" into wrapper
};

NamespaceTypeEntry::NamespaceTypeEntry(const QString &entryName, const QVersionNumber &vr,
                                       const TypeEntryCPtr &parent) :
    ComplexTypeEntry(new NamespaceTypeEntryPrivate(entryName, NamespaceType, vr, parent))
{
    S_D(NamespaceTypeEntry);
    d->m_copyableDetected = d->m_defaultConstructibleDetected = d->m_movableDetected = false;
    d->m_detectedDestructor = TypeSystem::DestructorType::NoDestructor;
}

TypeEntry *NamespaceTypeEntry::clone() const
{
    S_D(const NamespaceTypeEntry);
    return new NamespaceTypeEntry(new NamespaceTypeEntryPrivate(*d));
}

NamespaceTypeEntryCPtr NamespaceTypeEntry::extends() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_extends;
}

void NamespaceTypeEntry::setExtends(const NamespaceTypeEntryCPtr &e)
{
    S_D(NamespaceTypeEntry);
    d->m_extends = e;
}

const QRegularExpression &NamespaceTypeEntry::filePattern() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_filePattern;
}

void NamespaceTypeEntry::setFilePattern(const QRegularExpression &r)
{
    S_D(NamespaceTypeEntry);
    d->m_filePattern = r;
    d->m_hasPattern = !d->m_filePattern.pattern().isEmpty();
    if (d->m_hasPattern)
        d->m_filePattern.optimize();
}

bool NamespaceTypeEntry::hasPattern() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_hasPattern;
}

NamespaceTypeEntry::NamespaceTypeEntry(NamespaceTypeEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

bool NamespaceTypeEntry::matchesFile(const QString &needle) const
{
    S_D(const NamespaceTypeEntry);
    return d->m_filePattern.match(needle).hasMatch();
}

bool NamespaceTypeEntry::isVisible() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_visibility == TypeSystem::Visibility::Visible
        || (d->m_visibility == TypeSystem::Visibility::Auto && !d->m_inlineNamespace);
}

void NamespaceTypeEntry::setVisibility(TypeSystem::Visibility v)
{
    S_D(NamespaceTypeEntry);
    d->m_visibility = v;
}

bool NamespaceTypeEntry::isInlineNamespace() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_inlineNamespace;
}

void NamespaceTypeEntry::setInlineNamespace(bool i)
{
    S_D(NamespaceTypeEntry);
    d->m_inlineNamespace = i;
}

bool NamespaceTypeEntry::isVisibleScope(const TypeEntryCPtr &e)
{
    return isVisibleScope(e.get());
}

bool NamespaceTypeEntry::isVisibleScope(const TypeEntry *e)
{
    return e->type() != TypeEntry::NamespaceType
        || static_cast<const NamespaceTypeEntry *>(e)->isVisible();
}

bool NamespaceTypeEntry::generateUsing() const
{
    S_D(const NamespaceTypeEntry);
    return d->m_generateUsing;
}

void NamespaceTypeEntry::setGenerateUsing(bool generateUsing)
{
    S_D(NamespaceTypeEntry);
    d->m_generateUsing = generateUsing;
}

// ----------------- ValueTypeEntry

class ValueTypeEntryPrivate : public ComplexTypeEntryPrivate
{
public:
    using ComplexTypeEntryPrivate::ComplexTypeEntryPrivate;

    QString m_targetConversionRule;
    CustomConversionPtr m_customConversion;
};

ValueTypeEntry::ValueTypeEntry(const QString &entryName, const QVersionNumber &vr,
                               const TypeEntryCPtr &parent) :
    ComplexTypeEntry(new ValueTypeEntryPrivate(entryName, BasicValueType, vr, parent))
{
}

bool ValueTypeEntry::hasCustomConversion() const
{
    S_D(const ValueTypeEntry);
    return bool(d->m_customConversion);
}

void ValueTypeEntry::setCustomConversion(const CustomConversionPtr &customConversion)
{
    S_D(ValueTypeEntry);
    d->m_customConversion = customConversion;
}

CustomConversionPtr ValueTypeEntry::customConversion() const
{
    S_D(const ValueTypeEntry);
    return d->m_customConversion;
}

void ValueTypeEntry::setTargetConversionRule(const QString &conversionRule)
{
    S_D(ValueTypeEntry);
    d->m_targetConversionRule = conversionRule;
}

QString ValueTypeEntry::targetConversionRule() const
{
    S_D(const ValueTypeEntry);
    return d->m_targetConversionRule;
}

bool ValueTypeEntry::hasTargetConversionRule() const
{
    S_D(const ValueTypeEntry);
    return !d->m_targetConversionRule.isEmpty();
}

bool ValueTypeEntry::isValue() const
{
    return true;
}

TypeEntry *ValueTypeEntry::clone() const
{
    S_D(const ValueTypeEntry);
    return new ValueTypeEntry(new ValueTypeEntryPrivate(*d));
}

ValueTypeEntry::ValueTypeEntry(ComplexTypeEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

ValueTypeEntry::ValueTypeEntry(const QString &entryName, Type t, const QVersionNumber &vr,
                               const TypeEntryCPtr &parent) :
    ComplexTypeEntry(entryName, t, vr, parent)
{
}

// ----------------- FunctionTypeEntry
class FunctionTypeEntryPrivate : public TypeEntryPrivate
{
public:
    FunctionTypeEntryPrivate(const QString &entryName, const QString &signature,
                             const QVersionNumber &vr,
                             const TypeEntryCPtr &parent) :
         TypeEntryPrivate(entryName,  TypeEntry::FunctionType, vr, parent),
         m_signatures(signature)
    {
    }

    QStringList m_signatures;
    QString m_docFile;
};

FunctionTypeEntry::FunctionTypeEntry(const QString &entryName, const QString &signature,
                                     const QVersionNumber &vr,
                                     const TypeEntryCPtr &parent) :
    TypeEntry(new FunctionTypeEntryPrivate(entryName, signature, vr, parent))
{
}

void FunctionTypeEntry::addSignature(const QString &signature)
{
    S_D(FunctionTypeEntry);
    d->m_signatures << signature;
}

const QStringList &FunctionTypeEntry::signatures() const
{
     S_D(const FunctionTypeEntry);
     return d->m_signatures;
}

bool FunctionTypeEntry::hasSignature(const QString &signature) const
{
    S_D(const FunctionTypeEntry);
    return d->m_signatures.contains(signature);
}

QString FunctionTypeEntry::docFile() const
{
    S_D(const FunctionTypeEntry);
    return d->m_docFile;
}

void FunctionTypeEntry::setDocFile(const QString &df)
{
    S_D(FunctionTypeEntry);
    d->m_docFile = df;
}

TypeEntry *FunctionTypeEntry::clone() const
{
    S_D(const FunctionTypeEntry);
    return new FunctionTypeEntry(new FunctionTypeEntryPrivate(*d));
}

FunctionTypeEntry::FunctionTypeEntry(FunctionTypeEntryPrivate *d) :
    TypeEntry(d)
{
}

// ----------------- ObjectTypeEntry
ObjectTypeEntry::ObjectTypeEntry(const QString &entryName, const QVersionNumber &vr,
                                 const TypeEntryCPtr &parent)
    : ComplexTypeEntry(entryName, ObjectType, vr, parent)
{
    S_D(ComplexTypeEntry);
    d->m_defaultConstructibleDetected = d->m_copyableDetected = false;
}

TypeEntry *ObjectTypeEntry::clone() const
{
    S_D(const ComplexTypeEntry);
    return new ObjectTypeEntry(new ComplexTypeEntryPrivate(*d));
}

ObjectTypeEntry::ObjectTypeEntry(ComplexTypeEntryPrivate *d) :
    ComplexTypeEntry(d)
{
}

#ifndef QT_NO_DEBUG_STREAM

#define FORMAT_BOOL(name, var) \
    if (var) \
         debug << ", [" << name << ']';

#define FORMAT_NONEMPTY_STRING(name, var) \
    if (!var.isEmpty()) \
         debug << ", " << name << "=\"" << var << '"';

#define FORMAT_LIST_SIZE(name, var) \
    if (!var.isEmpty()) \
         debug << ", " << var.size() << ' '  << name;

void TypeEntry::formatDebug(QDebug &debug) const
{
    const QString cppName = qualifiedCppName();
    debug << '"' << m_d->m_name << '"';
    if (m_d->m_name != cppName)
        debug << "\", cppName=\"" << cppName << '"';
    debug << ", type=" << m_d->m_type << ", codeGeneration="
          << m_d->m_codeGeneration;
    const QString &targetName = targetLangName();
    if (m_d->m_name != targetName)
        debug << ", target=\"" << targetLangName() << '"';
    FORMAT_NONEMPTY_STRING("package", m_d->m_targetLangPackage)
    FORMAT_BOOL("stream", m_d->m_stream)
    FORMAT_BOOL("built-in", m_d->m_builtin)
    if (!m_d->m_version.isNull() && m_d->m_version > QVersionNumber(0, 0))
        debug << ", version=" << m_d->m_version;
    if (m_d->m_revision)
        debug << ", revision=" << m_d->m_revision;
    if (m_d->m_sbkIndex)
        debug << ", sbkIndex=" << m_d->m_sbkIndex;
    if (m_d->m_include.isValid())
        debug << ", include=" << m_d->m_include;
    if (m_d->m_private)
        debug << ", [private]";
    formatList(debug, "extraIncludes", m_d->m_extraIncludes, ", ");
}

void CppTypeEntry::formatDebug(QDebug &debug) const
{
    TypeEntry::formatDebug(debug);
    S_D(const CppTypeEntry);
    if (d->m_viewOn)
        debug << ", views=" << d->m_viewOn->name();
    if (isDefaultConstructible())
        debug << ", [default constructible]";
    if (isCopyable())
        debug << ", [copyable]";
    if (isMovable())
        debug << ", [movable]";
    if (isDestructible())
        debug << ", [destructible]";
}

void PrimitiveTypeEntry::formatDebug(QDebug &debug) const
{
    CppTypeEntry::formatDebug(debug);
    if (auto e = referencedTypeEntry()) {
        debug << ", references";
        for (; e ; e = e->referencedTypeEntry())
            debug << ":\"" << e->qualifiedCppName() <<'"';
    }
}

void ComplexTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const ComplexTypeEntry);

    ConfigurableTypeEntry::formatDebug(debug);
    FORMAT_BOOL("polymorphicBase", d->m_polymorphicBase)
    FORMAT_BOOL("genericClass", d->m_genericClass)
    if (d->m_deletionMode != TypeSystem::DeletionMode::Default)
        debug << ", deletionMode=" << unsigned(d->m_deletionMode);
    if (d->m_typeFlags != 0)
        debug << ", typeFlags=" << d->m_typeFlags;
    debug << ", except=" << int(d->m_exceptionHandling)
        << ", snakeCase=" << int(d->m_snakeCase);
    FORMAT_NONEMPTY_STRING("defaultSuperclass", d->m_defaultSuperclass)
    FORMAT_NONEMPTY_STRING("polymorphicIdValue", d->m_polymorphicIdValue)
    FORMAT_NONEMPTY_STRING("targetType", d->m_targetType)
    FORMAT_NONEMPTY_STRING("hash", d->m_hashFunction)
    FORMAT_LIST_SIZE("addedFunctions", d->m_addedFunctions)
    formatList(debug, "functionMods", d->m_functionMods, ", ");
    FORMAT_LIST_SIZE("codeSnips", d->m_codeSnips)
    FORMAT_LIST_SIZE("fieldMods", d->m_fieldMods)
}

void CustomTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const CustomTypeEntry);
    CppTypeEntry::formatDebug(debug);
    debug << ", checkFunction=" << d->m_checkFunction;
}

void PythonTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const PythonTypeEntry);

    CustomTypeEntry::formatDebug(debug);
    debug << ", type=" << int(d->m_cPythonType);
}

void FunctionTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const FunctionTypeEntry);

    TypeEntry::formatDebug(debug);
    debug << "signatures=" << d->m_signatures;
}

void TypedefEntry::formatDebug(QDebug &debug) const
{
    S_D(const TypedefEntry);

    ComplexTypeEntry::formatDebug(debug);
    debug << ", sourceType=\"" << d->m_sourceType << '"'
        << ", source=" << d->m_source << ", target=" << d->m_target;
}

void EnumTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const EnumTypeEntry);

    ConfigurableTypeEntry::formatDebug(debug);
    if (d->m_pythonEnumType != TypeSystem::PythonEnumType::Unspecified)
        debug << ", python-type=" << int(d->m_pythonEnumType);
    if (d->m_flags)
        debug << ", flags=(" << d->m_flags << ')';
}

void FlagsTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const FlagsTypeEntry);

    CppTypeEntry::formatDebug(debug);
    debug << ", \"" << d->m_originalName << "\", flags of \"" << d->m_enum->qualifiedCppName() << '"';
}

void NamespaceTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const NamespaceTypeEntry);

    ComplexTypeEntry::formatDebug(debug);
    auto pattern = d->m_filePattern.pattern();
    FORMAT_NONEMPTY_STRING("pattern", pattern)
    debug << ",visibility=" << d->m_visibility;
    if (d->m_inlineNamespace)
        debug << "[inline]";
}

QDebug operator<<(QDebug d, const OpaqueContainer &oc)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "OpaqueContainer(\"" << oc.name << "\": " << oc.templateParameters() << ')';
    return d;
}

void ContainerTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const ContainerTypeEntry);

    ComplexTypeEntry::formatDebug(debug);
    debug << ", type=" << d->m_containerKind << '"';
    if (!d->m_opaqueContainers.isEmpty())
        debug << ", opaque-containers=[" << d->m_opaqueContainers << ']';
}

void SmartPointerTypeEntry::formatDebug(QDebug &debug) const
{
    S_D(const SmartPointerTypeEntry);

    ComplexTypeEntry::formatDebug(debug);
    if (!d->m_instantiations.isEmpty()) {
        debug << "type=" << d->m_type << ", instantiations["
            << d->m_instantiations.size() << "]=(";
        for (const auto &i : d->m_instantiations) {
            debug << i.typeEntry->name() << ',';
            if (!i.name.isEmpty())
                debug << "=\"" << i.name << '"';
        }
        debug << ')';
    }
}

QDebug operator<<(QDebug d, const TypeEntry *te)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "TypeEntry(";
    if (te)
        te->formatDebug(d);
    else
        d << '0';
    d << ')';
    return d;
}

QDebug operator<<(QDebug d, const TypeEntryCPtr &te)
{
    d << te.get();
    return d;
}

QDebug operator<<(QDebug d, const TemplateEntry *te)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "TemplateEntry(";
    if (te) {
        d << '"' << te->name() << '"';
    } else {
        d << '0';
    }
    d << ')';
    return d;
}

QDebug operator<<(QDebug d, const TemplateEntryCPtr &te)
{
    d << te.get();
    return d;
}
#endif // QT_NO_DEBUG_STREAM
