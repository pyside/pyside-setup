// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkstatelock.h"

#ifdef Py_GIL_DISABLED

#include "sbkftoptions.h"
#include "sbkheldlocks.h"

#include <mutex>

/*!
    \group freethreading-statelock
    \title The state lock
    \ingroup freethreading

    \brief One lock for the bookkeeping the wrappers share.

    The parent/child graph, the ownership and validity flags and the
    referred-object map are shared by all threads. The state lock serializes
    the short transactions that read or write them.

    Code under the state lock calls no Python, decrefs nothing, runs no C++
    destructor, takes no other lock and does not attach or detach: a waiter
    holds its thread state, so a holder attaching during a stop-the-world
    would deadlock. Decrefs go into a DeferredDecrefs list that runs after
    the unlock; increments that only pin are allowed.

    Each graph function of libshiboken has a twin in basewrapper.cpp; a
    build with a GIL compiles the originals unchanged. These functions take
    the state lock, so a caller must not hold it:
    Shiboken::Object::getOwnership(), releaseOwnership(), setCppPointer(),
    setParent(), removeParent(), invalidate(), makeValid(), destroy(),
    deallocData(), keepReference(), removeReference(), clearReferences() and
    info().

    Debug builds track the raw locks each thread holds (checkLockRank(),
    noteLockAcquired(), noteLockReleased(), holdsLock(), reportLocksHeld(),
    checkNoRawLock()); in release builds these do nothing.
*/

namespace Shiboken {

// Defined here so that every library sees one mutex
static std::mutex &stateMutex()
{
    static std::mutex mutex;
    return mutex;
}

static bool stateLockEnabled()
{
    return FreeThreading::optionEnabled(FreeThreading::Option::StateLock);
}

/*!
    \class Shiboken::StateLockGuard
    \ingroup freethreading-statelock
    \inmodule libshiboken

    \brief Holds the state lock for its lifetime.
*/

/*!
    \class Shiboken::RelaxedFlag
    \ingroup freethreading-statelock
    \inmodule libshiboken

    \brief A flag of a wrapper that is written without the state lock.

    A one-byte bool, atomic with relaxed order, so that the flags of one
    wrapper are separate memory locations.
*/

/*!
    \ingroup freethreading-statelock
    \inmodule libshiboken

    Returns whether the calling thread holds the state lock. Release builds
    do not track it and return \c false.
*/
bool stateLockHeldByCurrentThread()
{
    return holdsLock(RawLock::State);
}

/*!
    \fn void Shiboken::stateLockAcquire()
    \ingroup freethreading-statelock
    \inmodule libshiboken

    Takes the state lock. Not recursive; debug builds assert on nesting.
*/
void stateLockAcquire()
{
    assert(!stateLockHeldByCurrentThread());
    checkLockRank(RawLock::State);
    if (stateLockEnabled())
        stateMutex().lock();
    noteLockAcquired(RawLock::State);
}

/*!
    \ingroup freethreading-statelock
    \inmodule libshiboken

    Releases the state lock, which the calling thread holds.
*/
void stateLockRelease()
{
    assert(stateLockHeldByCurrentThread());
    noteLockReleased(RawLock::State);
    if (stateLockEnabled())
        stateMutex().unlock();
}

/*!
    \class Shiboken::DeferredDecrefs
    \ingroup freethreading-statelock
    \inmodule libshiboken

    \brief The decrefs a state transaction decided on.

    Filled under the state lock and run after it with run(); the destructor
    runs what is left.
*/
DeferredDecrefs::~DeferredDecrefs()
{
    run();
}

/*!
    Adds a decref of \a o, whose reference the list now owns. Called under
    the state lock. A caller removes \a o from its container only after
    this returns: a failed allocation leaves the reference where it was.
*/
void DeferredDecrefs::addDecref(PyObject *o)
{
    assert(Shiboken::stateLockHeldByCurrentThread());
    m_objects.push_back(o);
}

/*!
    Decrefs the collected objects in the order they were added and empties
    the list. No raw lock may be held.
*/
void DeferredDecrefs::run()
{
    if (m_objects.empty())
        return;
    Shiboken::checkNoRawLock(__func__);
    // Taken over first: a decref runs arbitrary code
    std::vector<PyObject *> objects;
    objects.swap(m_objects);
    for (PyObject *o : objects)
        Py_DECREF(o);
}

} // namespace Shiboken

#endif // Py_GIL_DISABLED
