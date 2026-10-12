// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_MODULE_H
#define SBK_MODULE_H

#include "sbkpython.h"
#include "shibokenmacros.h"

extern "C"
{
struct SbkConverter;
}

namespace Shiboken::Module {

struct TypeInitStruct
{
    PyTypeObject *type;
    const char *fullName;
};

/*!
    Returns the type of \a typeStruct, creating it if needed.
    PYSIDE-2404: Replacing the arguments in cpythonTypeNameExt by a function.
*/
LIBSHIBOKEN_API PyTypeObject *get(TypeInitStruct &typeStruct);

/*!
    Makes sure that classes mentioned by \a name really exist (PYSIDE-2404).
    On free-threaded builds, it does nothing: every type exists once its
    module is imported.
*/
LIBSHIBOKEN_API void loadLazyClassesWithName(const char *name);

/*!
    Incarnates all classes of \a module for star imports (PYSIDE-2404).
    On free-threaded builds, it does nothing: every type exists once its
    module is imported.
*/
LIBSHIBOKEN_API void resolveLazyClasses(PyObject *module);

/*!
    Imports and returns the module named \a moduleName, or a nullptr in case
    of failure. If the module is already imported, it increments its reference
    count before returning it.
*/
LIBSHIBOKEN_API PyObject *import(const char *moduleName);

/*!
    Creates and returns a new Python module named \a moduleName using the
    information passed in \a moduleData and calls exec() on it.
*/
[[deprecated]] LIBSHIBOKEN_API PyObject *create(const char *moduleName, PyModuleDef *moduleData);

/*!
    Creates and returns a new Python module named \a moduleName using the
    information passed in \a moduleData. exec() is not called (Support for
    Nuitka).
*/
LIBSHIBOKEN_API PyObject *createOnly(const char *moduleName, PyModuleDef *moduleData);

/*!
    Executes the \a module (multi-phase initialization helper).
*/
LIBSHIBOKEN_API void exec(PyObject *module);

using TypeCreationFunction = PyTypeObject *(*)(PyObject *module);

/*!
    Adds a type creation function \a func for the type \a name to the \a module.
*/
LIBSHIBOKEN_API void AddTypeCreationFunction(PyObject *module,
                                             const char *name,
                                             TypeCreationFunction func);

LIBSHIBOKEN_API void AddTypeCreationFunction(PyObject *module,
                                             const char *enclosingName,
                                             TypeCreationFunction func,
                                             const char *subTypeNamePath);

/*!
    Registers the list of \a types (array of TypeInitStruct representing
    the types) created by \a module.
*/
LIBSHIBOKEN_API void registerTypes(PyObject *module, TypeInitStruct *types);

/*!
    Returns a pointer to the array of types created by \a module.
*/
LIBSHIBOKEN_API TypeInitStruct *getTypes(PyObject *module);

/*!
    Registers the list of \a converters (array of SbkConverter *) created by
    \a module for non-wrapper types.
*/
LIBSHIBOKEN_API void registerTypeConverters(PyObject *module, SbkConverter **converters);

/*!
    Returns a pointer to the array of converters created by \a module.
*/
LIBSHIBOKEN_API SbkConverter **getTypeConverters(PyObject *module);

/*!
    Replaces the dictionary of \a module by \a dict, changing its class to
    \a modClass. This allows for using \c __missing__.

    Returns true on success, false if the arguments are of the wrong type.
*/
LIBSHIBOKEN_API bool replaceModuleDict(PyObject *module, PyObject *modClass, PyObject *dict);

} // namespace Shiboken::Module

#endif // SBK_MODULE_H
