// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef OPTIONSPARSER_H
#define OPTIONSPARSER_H

#include <QtCore/QString>
#include <QtCore/QList>

QT_FORWARD_DECLARE_CLASS(QTextStream)

enum class OptionSource
{
    CommandLine, // "--option"
    CommandLineSingleDash, // "-o"
    ProjectFile
};

struct OptionDescription // For help formatting
{
    QString name;
    QString description;
};

using OptionDescriptions = QList<OptionDescription>;

QTextStream &operator<<(QTextStream &s, const OptionDescription &od);
QTextStream &operator<<(QTextStream &s, const OptionDescriptions &options);

class OptionsParser
{
public:
    Q_DISABLE_COPY_MOVE(OptionsParser)

    virtual ~OptionsParser();

    // Return true to indicate the option was processed.
    virtual bool handleBoolOption(const QString &key, OptionSource source);
    virtual bool handleOption(const QString &key, const QString &value, OptionSource source);

    static const QString &pathSyntax();

protected:
    OptionsParser() noexcept;
};

#endif // OPTIONSPARSER_H
