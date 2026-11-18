// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testwidget.h"

TestWidget::TestWidget(QWidget *parent) : QWidget(parent)
{
}

TestWidget *TestWidget::heuristicParentWidget() const
{
    return qobject_cast<TestWidget *>(parentWidget());
}

TestWidget *TestWidget::heuristicChildWidget() const
{
    return findChild<TestWidget *>();
}
