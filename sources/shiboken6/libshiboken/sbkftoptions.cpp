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
    \value ClassHierarchySnapshot
           A module import publishes its inheritance edges as a new
           snapshot: conversions walk the graph meanwhile.
    \value MiOffsetsOnce
           The multiple-inheritance offsets are computed once: two threads
           would fill them both.
    \value DocRecursionPerThread
           The recursion guard of \c __doc__ is per thread.
    \value PostRoutineBatch
           qAddPostRoutine() appends under a lock, and the routines run
           from a batch taken out under it.
    \value DictIterationSnapshot
           A conversion iterates a copy of a dict passed in, which another
           thread may change.
    \value QObjectMetaTypeRecords
           The QObject-pointer metatype hash is used under a lock: Qt reads it
           from any thread while QML registers.
    \value MroSnapshot
           A walk over a type's \c __mro__ or \c __bases__ owns the tuple,
           which a \c __bases__ assignment frees.
    \value ConverterRegistryLock
           The converter registry is used under a lock, and conversion lists
           are replaced as a whole: imports add both while others use them.
    \value ModuleRegistryLock
           The module tables are used under a lock: module execs run at once.
    \value ModuleImportWaits
           A module another one needs is imported through importlib, which
           waits for its exec in another thread.
    \value FirstUseOnce
           The one-time initializations run in one thread; others wait
           detached.
    \value ModuleDirDefault
           A module has no __dir__ of its own: another thread could call it
           before the module's tables exist.
    \value EnumPrivateLock
           The private data of enum types is used under a lock: imports and
           QEnum classes add enum types in any thread.
    \value QEnumCollectorPerThread
           The enums a class body marks with QEnum are collected per thread,
           where the class body runs.
    \value GenericEnumRegistryLock
           The generic enum registry is used under a lock: QEnum classes
           append while conversions read.
    \value StreamHelperOnce
           The QDataStream pickle helpers are published once: two first
           streams would build one each.
    \value WrapperWalkOwned
           A walk over all wrappers holds each one it collects, which another
           thread may drop.
    \value WrapperDumpLocked
           dumpWrapperMap() walks the map under its lock, which other threads
           change.
    \value MainThreadQueueLock
           The main-thread deletion queue is used under a lock and run from
           a copy: other threads queue meanwhile.
    \value ModuleDictKept
           replaceModuleDict() leaves the module alone and returns false:
           another thread could see it between its two stores. PySide6
           computes __all__ at import.
    \value MetaObjectBuilderLock
           Adding a dynamic signal or slot and building a meta object take a
           lock: two threads would write one builder.
    \value InstanceBuilderPublishOnce
           The first meta-object builder of an object is published once: two
           threads would make one each.
    \value ConnectionHashLock
           The slot connection hash is used under a lock; Python and Qt run
           outside it.
    \value MethodReceiverUpgrade
           A method slot holds its receiver per delivery; a receiver without
           weak references is held by the connection.
    \value SignalInstancePublishOnce
           The first signal instance an object binds is published once: two
           threads would make one each.
    \value SenderTrackerOnce
           The sender destruction tracker is created once: two first
           connections would make one each.
    \value RepeatedConnections
           Each connection of a method has its own row: a method connected
           twice is disconnected twice, and both go with the receiver.
    \value DynamicMethodOnce
           A dynamic signal or slot is added once, also when two threads add
           it.
    \value TypeBuilderLock
           A type's meta object is built under the builder lock also for a
           subtype or a registration.
    \value SourceDeletionPending
           A QObject handed to its owner thread for deletion counts as
           deleted as a signal source, as with a GIL.
    \value BuilderWarningUnlocked
           The signature warning is issued before the builder lock: a
           Python hook may wait for a thread that needs it.
    \value BuilderLookupUnlocked
           The builder is looked up before the builder lock: a key's
           \c __eq__ may wait for a thread that needs it.
    \value QmlPlacementFree
           A QML construction into given memory takes no process-wide lock:
           constructions in other threads would wait while Python runs.
    \value QmlPlacementType
           Only a QObject of the requested type takes QML's memory, not one
           built in its \c __init__().
    \value QmlImportGlobalsOwned
           The QML import globals of a module are held while read: another
           thread may rebind them.
    \value QmlHelperModuleOnce
           The helper modules of PySide6.QtQmlFeatures are published once:
           two first uses would store one each.
    \value SourcePropertyCopy
           A QtRemoteObjects source property is read as a copy and set by a
           swap: another thread may set it meanwhile.
    \value RepRegistryLock
           QtRemoteObjects dynamic types are made under a lock; Qt's name
           registry has none.
    \value SignalNameOnce
           The name of a signal attached after its class was made is set
           once: its first instances may be made in several threads.
    \value SignalClassList
           A new QObject takes its signals from a list kept per class, not
           from a copy of every class dict in the mro.
    \value SignalClassListCheck
           That list is only used while no class in the mro was written
           since it was made.
    \value ClassWriteCount
           A write to the dict or the bases of a class of Shiboken.ObjectType
           is counted, see Shiboken::ObjectType::ClassWrite.
    \value SignalClassListOwnBases
           A class with a base of another metatype gets no signal list: a
           write to that base is not counted.
    \value SignalClassListOrder
           A signal list takes the write count of a class before it reads
           the dict of that class.
    \value StaticInitDetached
           A thread that waits for the first value of a function-local
           static whose initializer can reach Python waits detached, see
           Shiboken::OnceValue.
    \value NewTypeQualname
           A type just made from a spec gets its __qualname__ without the
           setter, which stops the world on Python 3.15 for each type.
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
