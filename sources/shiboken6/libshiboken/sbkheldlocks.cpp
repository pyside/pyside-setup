// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkheldlocks.h"

#if defined(Py_GIL_DISABLED) && !defined(NDEBUG)

#include <array>
#include <cstdio>
#include <iterator>
#include <string>

/*!
    \enum Shiboken::RawLock
    \ingroup freethreading
    \inmodule libshiboken

    The raw locks in rank order: a thread only takes a lock that ranks above
    those it holds. None is held across Python, a decref or a C++
    destructor unless noted. Debug builds assert the rank order, and the
    second rule where checkNoRawLock() is called.

    \value ClassHierarchy    The class hierarchy, held to publish a snapshot.
    \value MetaObject        The meta-object builders (libpyside), recursive;
                             waiters stay attached. No Python runs under it.
    \value ConnectionHash    The slot connection hash (libpyside), container work only.
    \value WrapperMap        The wrapper map, recursive.
    \value MainThreadDelete  The main-thread deletion queue, one operation.
    \value State             The state lock, a leaf: nothing is taken under it.
    \value PostRoutine       The post-routine queue (QtCore), a leaf.
    \value QObjectMetaType   The QObject-pointer metatype hash (libpyside), a leaf.
    \value ConverterRegistry The named converter registry, a leaf.
    \value ModuleRegistry   The module tables, a leaf.
    \value EnumPrivate      The private data of enum types, a leaf.
    \value GenericEnumRegistry The Python enum types of the meta-object system (libpyside), a leaf.
    \value SenderTracker    The published sender tracker (libpyside), a leaf.
    \value PendingDeletion  The QObjects handed to their owner thread for
                            deletion (libpyside), a leaf.
*/

/*!
    \class Shiboken::TrackedGuard
    \ingroup freethreading
    \inmodule libshiboken

    \brief Holds \c Mutex as the raw lock \c Which for its lifetime.

    The guard checks the rank order on construction and records the lock as
    held by the calling thread; release builds only lock and unlock.
*/

/*!
    \class Shiboken::OnceFlag
    \ingroup freethreading
    \inmodule libshiboken

    \brief The state of a one-time initialization that runs Python.
*/

/*!
    \class Shiboken::OnceGate
    \ingroup freethreading
    \inmodule libshiboken

    \brief Lets one thread run a one-time initialization.

    The first thread runs it, others wait detached, and a nested call from
    the first thread passes. No raw lock may be held.
*/

namespace Shiboken
{

static constexpr std::size_t LockCount = 14;
// Index of the first leaf; the enum is in rank order up to it.
static constexpr std::size_t FirstLeaf = 5;

static std::array<unsigned, LockCount> &heldCounts()
{
    static thread_local std::array<unsigned, LockCount> counts{};
    return counts;
}

static std::size_t lockIndex(RawLock lock)
{
    auto bits = static_cast<unsigned>(lock);
    std::size_t index = 0;
    while ((bits >>= 1) != 0)
        ++index;
    return index;
}

static const char *lockName(RawLock lock)
{
    switch (lock) {
    case RawLock::ClassHierarchy:
        return "class hierarchy";
    case RawLock::MetaObject:
        return "meta object";
    case RawLock::ConnectionHash:
        return "connection hash";
    case RawLock::WrapperMap:
        return "wrapper map";
    case RawLock::MainThreadDelete:
        return "main-thread deletion";
    case RawLock::State:
        return "state";
    case RawLock::PostRoutine:
        return "post routines";
    case RawLock::QObjectMetaType:
        return "qobject-pointer metatype";
    case RawLock::ConverterRegistry:
        return "converter registry";
    case RawLock::ModuleRegistry:
        return "module registry";
    case RawLock::EnumPrivate:
        return "enum private data";
    case RawLock::GenericEnumRegistry:
        return "generic enum registry";
    case RawLock::SenderTracker:
        return "sender tracker";
    case RawLock::PendingDeletion:
        return "pending deletion";
    }
    return "unknown";
}

// The calling thread's count of lock
static unsigned &heldCount(RawLock lock)
{
    const std::size_t index = lockIndex(lock);
    assert(index < LockCount);
    return *std::next(heldCounts().begin(), index);
}

void checkLockRank(RawLock lock)
{
    // Rank order: a lower index is a lower rank; under a leaf nothing at all
    const std::size_t index = lockIndex(lock);
    std::size_t i = 0;
    for (const auto count : heldCounts()) {
        if (count != 0 && i != index) {
            assert((i < index && i < FirstLeaf)
                   || reportLocksHeld(lockName(lock), "taken out of rank order"));
        }
        ++i;
    }
}

void noteLockAcquired(RawLock lock)
{
    ++heldCount(lock);
}

void noteLockReleased(RawLock lock)
{
    auto &count = heldCount(lock);
    assert(count > 0);
    --count;
}

bool holdsLock(RawLock lock)
{
    return heldCount(lock) != 0;
}

/*!
    \fn const char *Shiboken::heldLockNames()
    \ingroup freethreading
    \inmodule libshiboken

    Returns the names of the raw locks the calling thread holds, or "none";
    empty in release builds. Shiboken.heldRawLocks() exposes it to tests.
*/
const char *heldLockNames()
{
    static thread_local std::string names;
    names.clear();
    unsigned bit = 1;
    for (const auto count : heldCounts()) {
        if (count != 0) {
            if (!names.empty())
                names += ", ";
            names += lockName(static_cast<RawLock>(bit));
        }
        bit <<= 1;
    }
    return names.empty() ? "none" : names.c_str();
}

bool reportLocksHeld(const char *where, const char *what)
{
    std::fprintf(stderr, "libshiboken: %s in %s(), but this thread holds: %s\n",
                 what, where, heldLockNames());
    return false;
}

void checkNoRawLock(const char *where)
{
    for (auto count : heldCounts()) {
        if (count != 0)
            assert(reportLocksHeld(where, "no raw lock may be held here"));
    }
}

} // namespace Shiboken

#endif // Py_GIL_DISABLED && !NDEBUG
