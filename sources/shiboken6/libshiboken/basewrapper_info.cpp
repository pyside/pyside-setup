// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "basewrapper.h"
#include "basewrapper_p.h"
#include "pep384impl.h"
#ifdef Py_GIL_DISABLED
#  include "sbkstatelock.h"
#endif

#include <iostream>
#include <sstream>

namespace Shiboken::Object {

static std::vector<PyTypeObject *> getBases(SbkObject *self)
{
    auto *type = Shiboken::pyType(self);
    return ObjectType::isUserType(type)
        ? getCppBaseClasses(type)
        : std::vector<PyTypeObject *>(1, type);
}

static bool isValueType(SbkObject *self)
{
    return PepType_SOTP(Shiboken::pyType(self))->type_behaviour == BEHAVIOUR_VALUETYPE;
}

void _debugFormat(std::ostream &s, SbkObject *self)
{
    assert(self);
    auto *d = self->d;
    if (!d) {
        s  << "[Invalid]";
        return;
    }
    const auto cptrs = cppPointers(self);
    if (!cptrs.empty() && cptrs[0]) {
        const std::vector<PyTypeObject *> bases = getBases(self);
        assert(cptrs.size() == bases.size());
        for (size_t i = 0, size = bases.size(); i < size; ++i)
            s << ", C++: " << bases[i]->tp_name << '/' << cptrs[i];
    } else {
         s << " [Deleted]";
    }
#ifdef Py_GIL_DISABLED
    // The graph under the state lock; the bases were read before it
    Shiboken::StateLockGuard guard;
#endif
    if (d->hasOwnership)
        s << " [hasOwnership]";
    if (d->containsCppWrapper)
        s << " [containsCppWrapper]";
    if (d->validCppObject)
        s << " [validCppObject]";
    if (d->cppObjectCreated)
        s << " [wasCreatedByPython]";
    s << (isValueType(self) ? " [value]" : " [object]");

    if (d->parentInfo) {
        if (auto *parent = d->parentInfo->parent)
            s << ", parent=" << reinterpret_cast<PyObject *>(parent)->ob_type->tp_name
                << '/' << parent;
        if (!d->parentInfo->children.empty())
            s << ", " << d->parentInfo->children.size() << " child(ren)";
    }
    if (d->referredObjects && !d->referredObjects->empty())
        s << ", " << d->referredObjects->size() << " referred object(s)";
}

static void info_format_preamble(std::ostream &s, SbkObject *self, const std::string &indent)
{
    s << indent << "id................ " << self << '\n';
    const auto cptrs = cppPointers(self);
    if (!cptrs.empty() && cptrs[0] != nullptr) {
        const std::vector<PyTypeObject *> bases = getBases(self);

        assert(cptrs.size() == bases.size());
        s << indent << "C++ address....... ";
        for (size_t i = 0, size = bases.size(); i < size; ++i)
            s << bases[i]->tp_name << '/' << cptrs[i] << ' ';
        s << "\n";
    }
    else {
        s << indent << "C++ address....... <<Deleted>>\n";
    }

    s << indent << "hasOwnership...... " << bool(self->d->hasOwnership) << '\n'
        << indent << "containsCppWrapper " << self->d->containsCppWrapper << '\n'
        << indent << "validCppObject.... " << self->d->validCppObject << '\n'
        << indent << "wasCreatedByPython " << self->d->cppObjectCreated << '\n'
        << indent << "value......        " << isValueType(self) << '\n'
        << indent << "reference count... " << Py_REFCNT(reinterpret_cast<PyObject *>(self)) << '\n';
}

static void info_format_parent(std::ostream &s, SbkObject *self, const std::string &indent)
{
    if (self->d->parentInfo && self->d->parentInfo->parent) {
        auto *obParent = reinterpret_cast<PyObject *>(self->d->parentInfo->parent);
        s << indent << "parent............ <" << Py_TYPE(obParent)->tp_name << " at " << obParent << ">\n";
    }
}

static void info_format_children(std::ostream &s, SbkObject *self, const std::string &indent)
{
    if (self->d->parentInfo && !self->d->parentInfo->children.empty()) {
        const auto &children = self->d->parentInfo->children;
        s << indent << "children.......... [" << children.size() << "] ";
        int n = 0;
        for (SbkObject *sbkChild : children) {
            auto *obChild = reinterpret_cast<PyObject *>(sbkChild);
            if (n++ > 5) {
                s << "...";
                break;
            }
            s << '<' << Py_TYPE(obChild)->tp_name << " at " << obChild << "> ";
        }
        s << '\n';
    }
}

static void info_format_referredObjects(std::ostream &s, SbkObject *self, const std::string &indent)
{
    if (self->d->referredObjects && !self->d->referredObjects->empty()) {
        const Shiboken::RefCountMap &map = *self->d->referredObjects;
        s << indent << "referred objects.. ";
        std::string lastKey;
        for (const auto &p : map) {
            if (p.first != lastKey) {
                if (!lastKey.empty())
                    s << "                   ";
                s << '"' << p.first << "\" => ";
                lastKey = p.first;
            }
            s << '<' << Py_TYPE(p.second)->tp_name << " at " << p.second << "> ";
        }
        s << '\n';
    }
}

static void dumpTreeRecursion(std::ostream &s, SbkObject *self, unsigned depth = 0)
{
    const std::string indent(4 * depth, ' ');
    info_format_preamble(s, self, indent);
    info_format_referredObjects(s, self, indent);
    if (self->d->parentInfo && !self->d->parentInfo->children.empty()) {
        s << indent << "Children:\n";
        for (SbkObject *sbkChild : self->d->parentInfo->children) {
            dumpTreeRecursion(s, sbkChild, depth + 1);
            s << '\n';
        }
    }
}

/*!
    Like info(), but creates a dump of the entire object tree starting from \a self.
 */
std::string dumpTree(SbkObject *self)
{
    std::ostringstream s;
    dumpTreeRecursion(s, self, 0);
    return s.str();
}

/*!
    Returns a string with information about the internal state of the instance
    object \a self, useful for debug purposes. On free-threaded builds, it
    takes the state lock, which the caller must not hold.
 */
std::string info(SbkObject *self)
{
    std::ostringstream s;
    const std::string indent;
    info_format_preamble(s, self, indent);
#ifdef Py_GIL_DISABLED
    // The graph under the state lock; the preamble reads the type hierarchy
    Shiboken::StateLockGuard guard;
#endif
    info_format_parent(s, self, indent);
    info_format_children(s, self, indent);
    info_format_referredObjects(s, self, indent);
    return s.str();
}

} // namespace Shiboken::Object
