// Copyright (C) 2025 Ford Motor Company
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef PYSIDE_CAPSULEMETHOD_P_H
#define PYSIDE_CAPSULEMETHOD_P_H

#include <sbkpython.h>

extern "C"
{

struct CapsuleDescriptorData
{
    PyObject *self;
    PyObject *payload;
};

/*!
    \brief Returns the new type defining a descriptor that stores a PyCapsule.

    This is used to store the runtime data, with the __get__ method returning a new Callable.
    \ingroup pysidecapsule
*/
PyTypeObject *CapsuleMethod_TypeF(void);

#ifdef Py_GIL_DISABLED
PyObject *capsuleMethodPayload(PyObject *self);
#endif

/*!
    \brief The new type defining a descriptor that stores a PyCapsule.

    This is used to store the runtime data, with the __get__ (and __set__ if
    \a isWritable) providing property behavior.
    \ingroup pysidecapsule
*/
PyTypeObject *CapsuleProperty_TypeF(bool isWritable);

/*!
    \brief Adds a capsule \a method (a descriptor) to a \a type.

    This will create a new capsule method descriptor and add it as an attribute
    to the type, using the given name.

    A single handle can then respond to what appear to be distinct methods on the
    type, but using the runtime data (from the capsule) when handling each call.

    Returns \c true if the descriptor was added successfully, \c false otherwise.

    \table
         \header
             \li Parameter
             \li Meaning
         \row
             \li \a type The type to attach the created descriptor to.
             \li \a method The method definition to associate with the descriptor.
                  The name of the method will be used as the attribute name.
             \li \a capsule The capsule to store in the descriptor.
    \endtable

    \ingroup pysidecapsule
*/
bool add_capsule_method_to_type(PyTypeObject *type, PyMethodDef *method,
                                PyObject *capsule);

/*!
    \brief Make a new CapsuleProperty type.

    \ingroup pysidecapsule
*/
PyObject *make_capsule_property(PyMethodDef *method, PyObject *capsule,
                                bool isWritable = false);

} // extern "C"

#endif // PYSIDE_CAPSULEMETHOD_P_H
