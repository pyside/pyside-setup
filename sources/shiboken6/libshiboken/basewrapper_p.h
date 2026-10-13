// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef BASEWRAPPER_P_H
#define BASEWRAPPER_P_H

#include "sbkpython.h"
#include "shibokenclasshelpermacros.h"
#include "basewrapper.h"
#ifdef Py_GIL_DISABLED
#  include "sbkrelaxedflag.h"
#endif

#include <unordered_map>
#include <set>
#if __cplusplus >= 202002L
#  include <span>
#else
#  include <sbkarrayspan_p.h>
#endif
#include <string>
#include <vector>
#include <iosfwd>

struct SbkObject;
struct SbkConverter;

namespace Shiboken
{
/*!
    \internal

    This mapping associates a method and argument of a wrapper object with the
    wrapper of said argument when it needs the binding to help manage its
    reference count.
*/
using RefCountMap = std::unordered_multimap<std::string, PyObject *> ;

/*!
    \internal

    Linked list of SbkBaseWrapper pointers.
*/
using ChildrenList = std::set<SbkObject *>;

/*!
    \internal

    Structure used to store information about object parent and children.
*/
struct ParentInfo
{
    /// Pointer to parent object.
    SbkObject *parent = nullptr;
    /// List of object children.
    ChildrenList children;
    /// has internal ref
    bool hasWrapperRef = false;
};

} // namespace Shiboken

extern "C"
{

/*!
    \internal

    Private data for SbkBaseWrapper.
*/
struct SbkObjectPrivate
{
    LIBSHIBOKEN_DISABLE_COPY_MOVE(SbkObjectPrivate)

    SbkObjectPrivate() noexcept = default;

#ifdef Py_GIL_DISABLED
    // One memory location each, for writers without the state lock.
    Shiboken::RelaxedFlag hasOwnership;
    Shiboken::RelaxedFlag containsCppWrapper;
    Shiboken::RelaxedFlag validCppObject;
    Shiboken::RelaxedFlag cppObjectCreated;
    Shiboken::RelaxedFlag isQAppSingleton;
#else
    /// True when Python is responsible for freeing the used memory.
    unsigned int hasOwnership : 1;
    /// This is true when the C++ class of the wrapped object has a virtual destructor AND was created by Python.
    unsigned int containsCppWrapper : 1;
    /// Marked as false when the object is lost to C++ and the binding can not know if it was deleted or not.
    unsigned int validCppObject : 1;
    /// Marked as true when the object constructor was called
    unsigned int cppObjectCreated : 1;
    /// PYSIDE-1470: Marked as true if this is the Q*Application singleton.
    /// This bit allows app deletion from shiboken?.delete() .
    unsigned int isQAppSingleton : 1;
#endif // Py_GIL_DISABLED
    /// Mirrored from SbkObjectTypePrivate; True for a Python class which inherits from several
    /// C++ classes.
    unsigned int is_multicpp : 1;

    /// Information about the object parents and children, may be null.
    Shiboken::ParentInfo *parentInfo;
    /// Manage reference count of objects that are referred to but not owned from.
    Shiboken::RefCountMap *referredObjects;

protected:
    ~SbkObjectPrivate()
    {
        delete parentInfo;
        parentInfo = nullptr;
        delete referredObjects;
        referredObjects = nullptr;
    }
};

// Standard wrapped class
struct SbkObjectPrivate1 : public SbkObjectPrivate
{
    LIBSHIBOKEN_DISABLE_COPY_MOVE(SbkObjectPrivate1)

    SbkObjectPrivate1() noexcept = default;
    ~SbkObjectPrivate1() = default;

    /// Pointer to the C++ class.
    void *cptr = nullptr;
};

// Python-created class using multiple wrapped C++ bases
struct SbkObjectMultiInheritancePrivate : public SbkObjectPrivate
{
    LIBSHIBOKEN_DISABLE_COPY_MOVE(SbkObjectMultiInheritancePrivate)

    explicit SbkObjectMultiInheritancePrivate(unsigned count) :
        cptrs(count, nullptr) {}
    ~SbkObjectMultiInheritancePrivate() = default;

    /// Pointers to the C++ classes.
    std::vector<void *> cptrs;
};

// TODO-CONVERTERS: to be deprecated/removed
/// The type behaviour was not defined yet
#define BEHAVIOUR_UNDEFINED 0
/// The type is a value type
#define BEHAVIOUR_VALUETYPE 1
/// The type is an object type
#define BEHAVIOUR_OBJECTTYPE 2

struct SbkObjectTypePrivate
{
    SbkConverter *converter;
    int *mi_offsets;
    MultipleInheritanceInitFunction mi_init;

    /// Special cast function, null if this class doesn't have multiple inheritance.
    SpecialCastFunction mi_specialcast;
    TypeDiscoveryFuncV2 type_discovery;
    /// Pointer to a function responsible for deletion of the C++ instance calling the proper destructor.
    ObjectDestructor cpp_dtor;
    /// C++ name
    char *original_name;
    /// Type user data
    void *user_data;
    DeleteUserDataFunc d_func;
    void (*subtype_init)(PyTypeObject *, PyObject *, PyObject *);
    const char **propertyStrings;
    const char **enumFlagInfo;
    PyObject *enumFlagsDict;
    PyObject *enumTypeDict;

    /// True if this type holds two or more C++ instances, e.g.: a Python class which inherits from two C++ classes.
    unsigned int is_multicpp : 1;
    /// True if this type was defined by the user (a class written in Python inheriting
    /// a class provided by a Shiboken binding).
    unsigned int is_user_type : 1;
    /// Tells is the type is a value type or an object-type, see BEHAVIOUR_ *constants.
    unsigned int type_behaviour : 2;
    unsigned int delete_in_main_thread : 1;
};

#ifdef Py_GIL_DISABLED
/// The metaclass the type factory makes types of Shiboken.ObjectType with,
/// see sbktypefactory.cpp
PyTypeObject *SbkObjectType_FactoryTypeF();
#endif

} // extern "C"

namespace Shiboken
{
inline void *cppPointer(SbkObjectPrivate *d)
{
    return d->is_multicpp ? static_cast<SbkObjectMultiInheritancePrivate *>(d)->cptrs[0]
                          : static_cast<SbkObjectPrivate1 *>(d)->cptr;
}

#if __cplusplus >= 202002L
using CPtrSpan = std::span<void *>;
#else
using CPtrSpan = ArraySpan<void *>;
#endif

/*!
    \internal

    Utility to return a span of the C++ pointers of the instance(s) \a ob.
*/
CPtrSpan cppPointersSpan(SbkObject *ob);

/*!
    \internal

    Utility to set the first C++ pointer of \a ob to \a cptr (unchecked).
*/
void setCppPointer(SbkObject *ob, void *cptr);

/*!
    \internal

    Utility to set the C++ pointers of the instance(s) \a ob to nullptr.
*/
void clearCppPointers(SbkObject *ob);

unsigned getNumberOfCppBaseClasses(PyTypeObject *baseType);

std::vector<PyTypeObject *> getCppBaseClasses(PyTypeObject *baseType);

namespace Object
{
/*!
    \internal

    Decrements the reference counters of every object referred by \a self,
    the wrapper instance that keeps references to other objects.
*/
void clearReferences(SbkObject *self);

/*!
    \internal

    Destroys the internal data of \a self, performing a cleanup
    if \a doCleanup is true.
*/
void deallocData(SbkObject *self, bool doCleanup);


void _debugFormat(std::ostream &str, SbkObject *self);
} // namespace Object

inline PyTypeObject *pyType(SbkObject *sbo)
{
    return Py_TYPE(reinterpret_cast<PyObject *>(sbo));
}

} // namespace Shiboken

#endif
