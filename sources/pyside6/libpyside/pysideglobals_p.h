// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDE_GLOBALS_P_H
#define PYSIDE_GLOBALS_P_H

#include <sbkpython.h>

#ifdef Py_GIL_DISABLED
#  include <atomic>
#endif

namespace PySide
{

struct Globals // Per interpreter globals of libpyside
{
    PyTypeObject *newFeatureDictType = nullptr;
    PyObject *featureDict = nullptr;
    PyObject *cachedFeatureGlobals = nullptr;
    PyTypeObject *lastFeatureType = nullptr;
    int lastSelectedFeatureId = 0;
    PyTypeObject *qobjectType = nullptr;
    PyObject *emptyTuple = nullptr;
#ifdef Py_GIL_DISABLED
    std::atomic<PyObject *> pickleReduceFunc{nullptr};
    std::atomic<PyObject *> pickleSafeLoadsFunc{nullptr};
#else
    PyObject *pickleReduceFunc = nullptr;
    PyObject *pickleSafeLoadsFunc = nullptr;
#endif
};

Globals *globals();

} //namespace PySide

#endif //PYSIDE_GLOBALS_P_H
