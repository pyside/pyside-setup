// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "testmodulegil.h"

#include <exception.h>
#include <optionsparser.h>
#include <typedatabase.h>
#include <typesystemtypeentry.h>

#include <QtTest/qtest.h>

#include <QtCore/qbuffer.h>

using namespace Qt::StringLiterals;

void TestModuleGil::testModuleGil_data()
{
    QTest::addColumn<QString>("option");
    QTest::addColumn<QString>("attribute");
    QTest::addColumn<TypeSystem::ModuleGil>("expected");

    QTest::newRow("default") << QString() << QString() << TypeSystem::ModuleGil::Used;
    QTest::newRow("option-yes") << u"yes"_s << QString() << TypeSystem::ModuleGil::Used;
    QTest::newRow("option-no") << u"no"_s << QString() << TypeSystem::ModuleGil::NotUsed;
    QTest::newRow("option-auto") << u"auto"_s << QString() << TypeSystem::ModuleGil::Auto;
    QTest::newRow("attribute-yes") << QString() << u"yes"_s << TypeSystem::ModuleGil::Used;
    QTest::newRow("attribute-false") << QString() << u"false"_s
        << TypeSystem::ModuleGil::NotUsed;
    QTest::newRow("attribute-auto") << QString() << u"auto"_s << TypeSystem::ModuleGil::Auto;
    QTest::newRow("attribute-overrides") << u"no"_s << u"auto"_s
        << TypeSystem::ModuleGil::Auto;
    QTest::newRow("attribute-invalid") << u"no"_s << u"junk"_s
        << TypeSystem::ModuleGil::NotUsed;
}

void TestModuleGil::testModuleGil()
{
    QFETCH(QString, option);
    QFETCH(QString, attribute);
    QFETCH(TypeSystem::ModuleGil, expected);

    auto *db = TypeDatabase::instance(true);
    if (!option.isEmpty())
        QVERIFY(db->createOptionsParser()->handleOption(u"module-uses-gil"_s, option,
                                                        OptionSource::CommandLine));
    QByteArray xmlCode = "<typesystem package=\"Foo\"";
    if (!attribute.isEmpty())
        xmlCode += " module-uses-gil=\"" + attribute.toUtf8() + '"';
    xmlCode += "/>";
    QBuffer buffer(&xmlCode);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QVERIFY(db->parseFile(&buffer));
    const auto entry = db->findTypeSystemType(u"Foo"_s);
    QVERIFY(entry);
    QCOMPARE(entry->moduleGil(), expected);
}

void TestModuleGil::testInvalidOption()
{
    auto parser = TypeDatabase::instance(true)->createOptionsParser();
    QVERIFY_THROWS_EXCEPTION(Exception,
                             parser->handleOption(u"module-uses-gil"_s, u"junk"_s,
                                                  OptionSource::CommandLine));
}

QTEST_APPLESS_MAIN(TestModuleGil)
