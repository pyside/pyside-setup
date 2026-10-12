// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#ifndef TESTMODULEGIL_H
#define TESTMODULEGIL_H

#include <QtCore/qobject.h>

class TestModuleGil : public QObject
{
    Q_OBJECT

private slots:
    void testModuleGil_data();
    void testModuleGil();
    void testInvalidOption();
};

#endif
