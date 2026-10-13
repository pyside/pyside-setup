// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef SBK_HELDLOCKS_H
#define SBK_HELDLOCKS_H

#include "sbkpython.h"
#include "shibokenmacros.h"

#ifdef Py_GIL_DISABLED

#include "sbkfailpoint.h"
#include "sbkftoptions.h"
#include "sbkstring.h"
#include "shibokenclasshelpermacros.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>

namespace Shiboken
{

enum class RawLock : std::uint16_t
{
    ClassHierarchy   = 1U << 0,
    WrapperMap       = 1U << 1,
    MainThreadDelete = 1U << 2,
    // The leaves: nothing may be taken while one of them is held.
    State            = 1U << 3,
    PostRoutine      = 1U << 4,
    QObjectMetaType  = 1U << 5,
    ConverterRegistry = 1U << 6,
    ModuleRegistry   = 1U << 7,
    EnumPrivate      = 1U << 8,
    GenericEnumRegistry = 1U << 9,
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

// Takes a lock under which Python can run, so that its holder may need the
// GIL: a thread that has to wait does so detached and takes the lock only
// attached again, like PyMutex. One that finalization keeps from attaching
// holds nothing. A raw lock admits no Python, and its waiters stay attached.
template <typename Mutex>
void lockDetached(Mutex &mutex)
{
    while (!mutex.try_lock()) {
        if (PyThreadState_GetUnchecked() == nullptr) {
            mutex.lock();
            return;
        }
        PyThreadState *saved = PyEval_SaveThread();
        mutex.lock();
        mutex.unlock();
        PyEval_RestoreThread(saved);
    }
}

// A one-time init that runs Python: with a plain flag, two module inits would
// both run it, or one would use what the other half did. The first thread
// runs it, others wait detached (it may stop the world), a nested call passes.
// Not a raw lock: Python runs under it.
struct OnceFlag
{
    std::mutex mutex;
    std::atomic<bool> done{false};
    std::atomic<std::thread::id> owner{};

    // Not done, and not running in this thread
    bool pending() const
    {
        return !done.load(std::memory_order_acquire)
            && owner.load(std::memory_order_relaxed) != std::this_thread::get_id();
    }
};

class OnceGate
{
public:
    // finished(): whether the init got through, else the first pass counts;
    // a failed init leaves the gate in place
    explicit OnceGate(OnceFlag &flag, bool (*finished)() = nullptr)
        : m_finished(finished)
    {
        if (!FreeThreading::optionEnabled(FreeThreading::Option::FirstUseOnce)
            || flag.done.load(std::memory_order_acquire)
            || flag.owner.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            return;
        }
        checkNoRawLock("OnceGate");
        lockDetached(flag.mutex);
        if (flag.done.load(std::memory_order_acquire)) {
            flag.mutex.unlock();
            return;
        }
        flag.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
        m_flag = &flag;
    }

    ~OnceGate()
    {
        if (m_flag == nullptr)
            return;
        m_flag->owner.store({}, std::memory_order_relaxed);
        if (m_finished == nullptr || m_finished())
            m_flag->done.store(true, std::memory_order_release);
        m_flag->mutex.unlock();
    }

    LIBSHIBOKEN_DISABLE_COPY_MOVE(OnceGate)

private:
    OnceFlag *m_flag = nullptr;
    bool (*m_finished)() = nullptr;
};

// For a function-local static whose initializer can reach Python. A second
// thread waits on the guard of a C++ static attached, so a stop-the-world or a
// lock it holds stops the first thread for good. Here the first thread creates
// the value and the others wait detached. A null result is kept, as by the
// static; a nested first use in the creating thread deadlocks, as there.
template <typename T>
class OnceValue
{
    static_assert(std::is_pointer_v<T>);

public:
    template <typename Create>
    T get(Create create)
    {
        if (T result = m_value.load(std::memory_order_acquire))
            return result;
        if (m_done.load(std::memory_order_acquire))
            return m_value.load(std::memory_order_relaxed);
        if (FreeThreading::optionEnabled(FreeThreading::Option::StaticInitDetached)) {
            checkNoRawLock("OnceValue");
            lockDetached(m_mutex);
        } else {
            m_mutex.lock(); // The A/B side: wait attached, as on a C++ static
        }
        std::lock_guard<std::mutex> guard(m_mutex, std::adopt_lock);
        if (!m_done.load(std::memory_order_relaxed)) {
            Shiboken::failpoint("once-value-create");
            m_value.store(create(), std::memory_order_release);
            m_done.store(true, std::memory_order_release);
        }
        return m_value.load(std::memory_order_relaxed);
    }

private:
    std::atomic<T> m_value{nullptr};
    std::atomic<bool> m_done{false};
    std::mutex m_mutex;
};

// A function-local static string, interned on first use through OnceValue
class OnceString
{
public:
    constexpr explicit OnceString(const char *string) noexcept : m_string(string) {}

    operator PyObject *() const
    {
        return m_value.get([this] { return String::createStaticString(m_string); });
    }

private:
    const char *m_string;
    mutable OnceValue<PyObject *> m_value;
};

} // namespace Shiboken

#define SBK_ASSERT_LOCK_NOT_HELD(lock) \
    assert(!Shiboken::holdsLock(Shiboken::RawLock::lock) \
           || Shiboken::reportLocksHeld(__func__, "the " #lock \
                                        " lock may not be held here"))

#endif // Py_GIL_DISABLED

#endif // SBK_HELDLOCKS_H
