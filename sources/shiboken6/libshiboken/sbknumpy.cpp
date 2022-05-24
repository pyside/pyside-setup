// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only


#ifdef HAVE_NUMPY
// Include numpy first to get the proper PyArray_Check
#  include <numpy/arrayobject.h>
#endif

#include "sbknumpycheck.h"
#include "sbknumpyview.h"

namespace Shiboken::Numpy
{

bool check(PyObject *pyIn)
{
#ifdef HAVE_NUMPY
    return PyArray_Check(pyIn);
#else
    return false;
#endif
}

} //namespace Shiboken::Numpy

// Include all sources files using numpy so that they are in the same
// translation unit (see comment at initNumPyArrayConverters()).

#include "sbknumpyview.cpp"
#ifdef HAVE_NUMPY
#  include "sbknumpyarrayconverter.cpp"
#endif
