// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#ifndef ENUMTYPEENTRY_H
#define ENUMTYPEENTRY_H

#include "configurabletypeentry.h"
#include "typesystem_enums.h"

class EnumTypeEntryPrivate;

// EnumTypeEntry is configurable for global enums only
class EnumTypeEntry : public ConfigurableTypeEntry
{
public:
    enum AliasMode : unsigned char {
        NoAlias,
        AliasSource, // Source of a C++ alias "using ThatEnum = ThisEnum";
        AliasTarget // Target of a C++ alias "using ThisEnum = ThatEnum";
    };

    explicit EnumTypeEntry(const QString &entryName,
                           const QVersionNumber &vr,
                           const TypeEntryCPtr &parent);

    TypeSystem::PythonEnumType pythonEnumType() const;
    void setPythonEnumType(TypeSystem::PythonEnumType t);

    QString targetLangQualifier() const;

    QString qualifier() const;

    EnumValueTypeEntryCPtr nullValue() const;
    void setNullValue(const EnumValueTypeEntryCPtr &n);

    void setFlags(const FlagsTypeEntryPtr &flags);
    FlagsTypeEntryPtr flags() const;

    QString cppType() const;
    void setCppType(const QString &t);

    void setForceUnsigned(bool f);
    bool forceUnsigned() const;

    bool isEnumValueRejected(const QString &name) const;
    void addEnumValueRejection(const QString &name);
    QStringList enumValueRejections() const;

    QString docFile() const;
    void setDocFile(const QString &df);

    AliasMode aliasMode() const;
    void setAliasMode(AliasMode am);

    EnumTypeEntryCPtr aliasTypeEntry() const;
    void setAliasTypeEntry(const EnumTypeEntryCPtr &entry);

    TypeEntry *clone() const override;
#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif
protected:
    explicit EnumTypeEntry(EnumTypeEntryPrivate *d);
};

#endif // ENUMTYPEENTRY_H
