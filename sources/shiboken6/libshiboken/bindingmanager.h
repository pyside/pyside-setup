// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef BINDINGMANAGER_H
#define BINDINGMANAGER_H

#include "sbkpython.h"
#include "shibokenclasshelpermacros.h"
#include "shibokenmacros.h"
#include "sbkdestructorentry.h"
#include "sbkwrapperref.h"

#include <set>
#include <utility>
#ifdef Py_GIL_DISABLED
#  include <vector>
#endif

struct SbkObject;

namespace Shiboken
{

namespace Module {
struct TypeInitStruct;
}

using ObjectVisitor = void (*)(SbkObject *, void *);

class LIBSHIBOKEN_API BindingManager
{
public:
    LIBSHIBOKEN_DISABLE_COPY_MOVE(BindingManager)

    static BindingManager &instance();

    bool hasWrapper(const void *cptr, PyTypeObject *typeObject) const;
    bool hasWrapper(const void *cptr) const;

    void registerWrapper(SbkObject *pyObj, void *cptr);
    void unregisterWrapper(SbkObject *sbkObj);
    void releaseWrapper(SbkObject *sbkObj);

    void runDeletionInMainThread();
    void addToDeletionInMainThread(const DestructorEntry &);

#ifdef Py_GIL_DISABLED
    [[nodiscard]] WrapperRef wrapperRef(const void *cptr, PyTypeObject *typeObject) const;
    [[nodiscard]] WrapperRef wrapperRef(const void *cptr) const;
    [[nodiscard]] PyTypeObject *wrapperTypeRef(const void *cptr) const;

    struct Registration
    {
        WrapperRef winner;

        explicit operator bool() const { return bool(winner); }
    };
    [[nodiscard]] Registration registerWrapperUnlessPresent(SbkObject *pyObj, void *cptr,
                                                            PyTypeObject *typeObject);
#else // Py_GIL_DISABLED
    SbkObject *retrieveWrapper(const void *cptr, PyTypeObject *typeObject) const;
    SbkObject *retrieveWrapper(const void *cptr) const;

    [[nodiscard]] WrapperRef wrapperRef(const void *cptr, PyTypeObject *typeObject) const
    { return WrapperRef::fromBorrowed(retrieveWrapper(cptr, typeObject)); }
    [[nodiscard]] WrapperRef wrapperRef(const void *cptr) const
    { return WrapperRef::fromBorrowed(retrieveWrapper(cptr)); }
#endif // Py_GIL_DISABLED
    static PyObject *getOverride(SbkObject *wrapper, PyObject *pyMethodName);

    void addClassInheritance(Module::TypeInitStruct *parent, Module::TypeInitStruct *child);
    using TypeCptrPair = std::pair<PyTypeObject *, void *>;
    TypeCptrPair findDerivedType(void *cptr, PyTypeObject *type) const;

    static void *runTypeDiscovery(void *cptr, PyTypeObject *probeType, PyTypeObject *type);

    [[deprecated]] PyTypeObject *resolveType(void **cptr, PyTypeObject *type);

#ifdef Py_GIL_DISABLED
    std::vector<WrapperRef> getAllPyObjects();
#else
    std::set<PyObject *> getAllPyObjects();
#endif

    void visitAllPyObjects(ObjectVisitor visitor, void *data);

    bool dumpTypeGraph(const char *fileName) const;
    void dumpWrapperMap();

private:
    ~BindingManager();
    BindingManager();

    struct BindingManagerPrivate;
    BindingManagerPrivate *m_d;
};

LIBSHIBOKEN_API bool callInheritedInit(PyObject *self, PyObject *args, PyObject *kwds,
                                       Module::TypeInitStruct typeStruct);

} // namespace Shiboken

#endif // BINDINGMANAGER_H
