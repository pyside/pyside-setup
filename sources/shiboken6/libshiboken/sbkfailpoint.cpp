// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkfailpoint.h"

#if defined(Py_GIL_DISABLED) && !defined(SBK_NO_FAILPOINTS)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

namespace Shiboken
{

// check_failpoint_names.py keeps this list and
// the Shiboken::failpoint() call sites in step.
static constexpr std::array<const char *, 1> KnownFailpoints = {
    "delete-later-before-post",
};

struct FailpointState
{
    std::mutex mutex;
    std::condition_variable released;
    std::string armed;      // taken by the first thread that arrives
    std::string parked;     // where a thread waits right now
    bool release = false;
    int timeoutMs = 5000;
    std::atomic<bool> anyArmed{false}; // fast path: no mutex while unarmed
};

static FailpointState &state()
{
    static FailpointState s;
    return s;
}

static bool isKnown(const char *name)
{
    return std::any_of(KnownFailpoints.cbegin(), KnownFailpoints.cend(),
                       [name](const char *known) { return std::strcmp(known, name) == 0; });
}

/*!
    \fn bool Shiboken::armFailpoint(const char *name, int timeoutMs)
    \ingroup freethreading
    \inmodule libshiboken

    Arms the failpoint \a name: the next thread to reach it waits until
    releaseFailpoint() or \a timeoutMs. Returns \c false if this build has
    no such point; only debug builds without the limited API have
    failpoints. One point is armed at a time.
*/
bool armFailpoint(const char *name, int timeoutMs)
{
    if (!isKnown(name))
        return false;
    auto &s = state();
    std::lock_guard<std::mutex> guard(s.mutex);
    s.armed = name;
    s.parked.clear();
    s.release = false;
    s.timeoutMs = timeoutMs;
    s.anyArmed.store(true, std::memory_order_release);
    return true;
}

/*!
    \fn bool Shiboken::releaseFailpoint(const char *name)
    \ingroup freethreading
    \inmodule libshiboken

    Disarms \a name and lets a thread waiting there go on. Returns whether a
    thread was waiting.
*/
bool releaseFailpoint(const char *name)
{
    auto &s = state();
    bool wasParked = false;
    {
        std::lock_guard<std::mutex> guard(s.mutex);
        if (s.armed != name && s.parked != name)
            return false;
        wasParked = s.parked == name;
        s.armed.clear();
        s.anyArmed.store(false, std::memory_order_release);
        s.release = true;
    }
    s.released.notify_all();
    return wasParked;
}

/*!
    \fn bool Shiboken::failpointReached(const char *name)
    \ingroup freethreading
    \inmodule libshiboken

    Returns whether a thread waits at \a name right now.
*/
bool failpointReached(const char *name)
{
    auto &s = state();
    std::lock_guard<std::mutex> guard(s.mutex);
    return s.parked == name;
}

/*!
    \fn void Shiboken::clearFailpoints()
    \ingroup freethreading
    \inmodule libshiboken

    Disarms every point and releases a waiting thread.
*/
void clearFailpoints()
{
    auto &s = state();
    {
        std::lock_guard<std::mutex> guard(s.mutex);
        s.armed.clear();
        s.anyArmed.store(false, std::memory_order_release);
        s.release = true;
    }
    s.released.notify_all();
}

/*!
    \fn const char *Shiboken::failpointNames()
    \ingroup freethreading
    \inmodule libshiboken

    Returns the names of the failpoints in this build, comma-separated.
*/
const char *failpointNames()
{
    static const std::string all = [] {
        std::string result;
        for (const char *name : KnownFailpoints) {
            if (!result.empty())
                result += ',';
            result += name;
        }
        return result;
    }();
    return all.c_str();
}

/*!
    \fn void Shiboken::failpoint(const char *name)
    \ingroup freethreading
    \inmodule libshiboken

    Waits here if \a name is armed, detached from the interpreter. Called
    from the code under test.
*/
void failpoint(const char *name)
{
    auto &s = state();
    if (!s.anyArmed.load(std::memory_order_acquire))
        return;
    std::unique_lock<std::mutex> guard(s.mutex);
    if (s.armed != name)
        return;
    s.armed.clear();
    s.anyArmed.store(false, std::memory_order_release);
    s.parked = name;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(s.timeoutMs);
    // Detached: an attached waiter would block stop-the-world
    PyThreadState *saved = PyGILState_Check() ? PyEval_SaveThread() : nullptr;
    s.released.wait_until(guard, deadline, [&s] { return s.release; });
    s.parked.clear();
    // Reattaching can wait for stop-the-world: not under our mutex
    guard.unlock();
    if (saved != nullptr)
        PyEval_RestoreThread(saved);
}

} // namespace Shiboken

#endif // Py_GIL_DISABLED && !SBK_NO_FAILPOINTS
