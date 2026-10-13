// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkftoptions.h"

#ifdef Py_GIL_DISABLED

#include <cstdlib>
#include <string_view>

/*!
    \group freethreading
    \title Free-threaded builds

    \brief What libshiboken adds when Python runs without a GIL.

    Only where \c Py_GIL_DISABLED is defined. Free-threaded builds have no
    lazy loading and no \c __feature__. Each measure has a bit in
    Shiboken::FreeThreading::Option.
*/

/*!
    \namespace Shiboken::FreeThreading
    \inmodule libshiboken
    \ingroup freethreading

    \brief The option bits of the free-threaded build.
*/

namespace Shiboken::FreeThreading {

/*!
    \enum Shiboken::FreeThreading::Option
    \ingroup freethreading
    \inmodule libshiboken

    The measures of the free-threaded build, one bit each. The environment
    variable \c PYSIDE6_OPTION_FT selects them: unset or \c on means all,
    \c off none, a number the bits it sets, and \c{~}\e{number} all but
    those. Clearing a bit is a testing device; only all bits set is
    supported.

    \value OwnerThreadDeletion
           A Python-owned QObject is deleted in its own thread
           (setDeleteInOwnerThread()).
    \value FinishedOwnerCleanup
           A QObject handed to its owner thread while that thread finishes
           is deleted once it has finished, if its deferred deletion missed
           the thread's last flush.
    \value StateLock
           The state lock serializes the bookkeeping wrappers share (see
           \l{The state lock}).
    \value DictPublishOnce
           A wrapper's instance dict is created once and never replaced.
    \value OwnedWrapperLookup
           A wrapper map lookup takes its reference with
           PyUnstable_TryIncRef(), so it never revives a dying wrapper.
    \value OneWrapperPerAddress
           One C++ object gets one wrapper, also when two threads convert it.
    \value ConstructorInstallOnce
           A second \c __init__, from any thread, is refused before a Python
           \c __init__ or a C++ constructor runs: two threads would both
           install one.
    \value ClassAssignCheck
           A \c __class__ assignment to a class with other C++ bases is
           refused: the wrapper's C++ objects follow its original bases.
    \value InvalidateRootsOwned
           Shiboken.invalidate() holds the objects of the list it walks, which
           another thread may change.
    \value ContainerArgumentCopy
           A generated function works on a private copy of a list, dict or
           set container argument: another thread may change it during the
           call.
*/

static std::uint64_t parseOption(const char *e)
{
    constexpr std::uint64_t all = ~std::uint64_t{0};
    // Unset means every bit, also those added later
    if (e == nullptr || *e == '\0')
        return all;

    std::string_view v{e};
    if (v == "yes" || v == "on" || v == "true")
        return all;
    if (v == "no" || v == "off" || v == "false")
        return 0;

    const bool invert = v.front() == '~';
    if (invert)
        v.remove_prefix(1);

    int base = 10;
    std::size_t offset = 0;
    if (v.size() > 2 && v[0] == '0') {
        if (v[1] == 'b' || v[1] == 'B') {
            base = 2;
            offset = 2;
        } else if (v[1] == 'x' || v[1] == 'X') {
            base = 16;
            offset = 2;
        }
    }
    const std::uint64_t flags = std::strtoull(v.data() + offset, nullptr, base);
    return invert ? ~flags : flags;
}

/*!
    \ingroup freethreading
    \inmodule libshiboken

    Returns whether the measure \a opt is enabled. \c PYSIDE6_OPTION_FT is
    read once, on first use.
*/
bool optionEnabled(Option opt)
{
    return (optionFlags() & static_cast<std::uint64_t>(opt)) != 0;
}

/*!
    \ingroup freethreading
    \inmodule libshiboken

    Returns the bits \c PYSIDE6_OPTION_FT selects, read once, on first use.
*/
std::uint64_t optionFlags()
{
    static const std::uint64_t flags = parseOption(std::getenv("PYSIDE6_OPTION_FT"));
    return flags;
}

} // namespace Shiboken::FreeThreading

#endif // Py_GIL_DISABLED
