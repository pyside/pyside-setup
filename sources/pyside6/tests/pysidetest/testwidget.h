// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef TESTWIDGET_H
#define TESTWIDGET_H

#include "pysidetest_macros.h"

#include <QtWidgets/qwidget.h>

class PYSIDETEST_API TestWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TestWidget(QWidget *parent = nullptr);

    // Trigger return value heuristics
    TestWidget *heuristicParentWidget() const;
    TestWidget *heuristicChildWidget() const;
};

#endif // TESTWIDGET_H
