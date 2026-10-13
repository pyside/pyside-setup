// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDEQAPP_H
#define PYSIDEQAPP_H

#include <pysidemacros.h>

namespace PySide
{

PYSIDE_API void initQApp();

/*!
    Destroys a QCoreApplication taking care of destroying all instances
    of QObject first. On free-threaded builds, a QObject of another running
    thread is left to that thread.
*/
PYSIDE_API void destroyQCoreApplication();

} //namespace PySide

#endif // PYSIDEQPP_H
