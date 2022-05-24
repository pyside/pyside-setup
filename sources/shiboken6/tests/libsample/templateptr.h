// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef TEMPLATEPTR_H
#define TEMPLATEPTR_H

#include <utility>
#include <list>
#include "libsamplemacros.h"
#include "blackbox.h"

class LIBSAMPLE_API TemplatePtr
{
public:
	void dummy(std::list<std::pair<BlackBox *, BlackBox *> > & items);
};

#endif
