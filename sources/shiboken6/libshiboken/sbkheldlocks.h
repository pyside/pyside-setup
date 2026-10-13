// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_HELDLOCKS_H
#define SBK_HELDLOCKS_H

#include "sbkpython.h"
#include "shibokenmacros.h"

#ifdef Py_GIL_DISABLED

#include "shibokenclasshelpermacros.h"

#include <cassert>
#include <cstdint>

namespace Shiboken
{

enum class RawLock : std::uint16_t
{
    WrapperMap       = 1U << 0,
    MainThreadDelete = 1U << 1,
    State            = 1U << 2,
};

#ifndef NDEBUG

LIBSHIBOKEN_API void checkLockRank(RawLock lock);
LIBSHIBOKEN_API void noteLockAcquired(RawLock lock);
LIBSHIBOKEN_API void noteLockReleased(RawLock lock);
LIBSHIBOKEN_API bool holdsLock(RawLock lock);
LIBSHIBOKEN_API const char *heldLockNames();
LIBSHIBOKEN_API bool reportLocksHeld(const char *where, const char *what);
LIBSHIBOKEN_API void checkNoRawLock(const char *where);

#else

inline void checkLockRank(RawLock) {}
inline void noteLockAcquired(RawLock) {}
inline void noteLockReleased(RawLock) {}
inline bool holdsLock(RawLock) { return false; }
inline const char *heldLockNames() { return ""; }
inline bool reportLocksHeld(const char *, const char *) { return false; }
inline void checkNoRawLock(const char *) {}

#endif // NDEBUG

template <typename Mutex, RawLock Which>
class TrackedGuard
{
public:
    explicit TrackedGuard(Mutex &mutex) : m_mutex(mutex)
    {
        checkLockRank(Which);
        m_mutex.lock();
        noteLockAcquired(Which);
    }
    ~TrackedGuard()
    {
        noteLockReleased(Which);
        m_mutex.unlock();
    }
    LIBSHIBOKEN_DISABLE_COPY_MOVE(TrackedGuard)
private:
    Mutex &m_mutex;
};

} // namespace Shiboken

#define SBK_ASSERT_LOCK_NOT_HELD(lock) \
    assert(!Shiboken::holdsLock(Shiboken::RawLock::lock) \
           || Shiboken::reportLocksHeld(__func__, "the " #lock \
                                        " lock may not be held here"))

#endif // Py_GIL_DISABLED

#endif // SBK_HELDLOCKS_H
