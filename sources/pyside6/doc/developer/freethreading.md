# Free-threaded builds

CPython can be built without the global interpreter lock (GIL). Such a
free-threaded interpreter is installed under its own name, for example
`python3.14t`, and PySide can be built with it. The support is experimental and
opt-in: only a build made with a free-threaded interpreter has it. A build made
with a regular interpreter (a GIL build) compiles exactly the same code as
PySide without free-threading support, and everything below applies to
free-threaded builds only.

Some terms used below: a wrapper is the Python object that stands for a C++
object. A QObject is owned by Python when PySide deletes it together with its
wrapper. A thread is attached while it may run Python code (in CPython's terms,
it holds a thread state). A measure is one change that makes PySide safe
without a GIL; each measure can be switched off for testing (see "Building and
testing"). "Without it" below means with that measure switched off.

## What changes

### Imports

- Importing PySide does not switch the GIL on. A free-threaded interpreter
  switches it on for the whole process, with a `RuntimeWarning`, when it
  imports a module that does not declare `Py_MOD_GIL_NOT_USED` in its
  `Py_mod_gil` slot. shiboken writes that declaration into the PySide6 and
  shiboken6 modules; QtQmlFeatures declares it in its own source.
- With `PYTHON_GIL=1` or `-X gil=1` the GIL is on anyway. All measures stay in
  effect, and lazy loading and `__feature__` stay unavailable.
- Lazy loading is off. An import creates all types of its module at once, and
  `PYSIDE6_OPTION_LAZY` has no effect.
- `from __feature__ import ...` raises `ImportError`, so `snake_case` and
  `true_property` cannot be used.
- Two bindings made with shiboken can be imported for the first time in two
  threads at once. libshiboken is set up in one of them while the other waits;
  without it the imports hang or crash (`FirstUseOnce`).
- Values can be converted while another thread imports a module. Imports add
  converters, so the converter registry is used under a lock; without it a
  conversion fails with "Can't find converter" (`ConverterRegistryLock`).
- `Shiboken.replaceModuleDict()` leaves the module unchanged and returns
  `False`, so no thread sees the `PySide6` package half replaced. The package
  computes `__all__` during its import (`ModuleDictKept`).

### Objects

- A QObject owned by Python is deleted in its own thread, also when its last
  reference goes in another one. Without it, the QObject is deleted in the
  thread that drops the reference, and the application crashes
  (`OwnerThreadDeletion`). See Limits for when the deletion runs.
- An object dropped while its own thread is finishing is deleted after that
  thread has finished; without it the object leaks (`FinishedOwnerCleanup`).
- The garbage collector can free objects in one thread while another thread
  changes their parents. Parent/child relations and ownership are changed
  under a lock for this; without it the application crashes (`StateLock`).
- A thread that gets a C++ object back from Qt, for example from
  `app.thread()`, while another thread deletes its Python wrapper, gets a new
  wrapper. Without it the dying wrapper is revived, and the application
  crashes (`OwnedWrapperLookup`).
- An argument of exact type `list`, `dict` or `set` for a C++ container
  parameter is copied, and the copy is kept until the call returns. Another
  thread changing the original does not affect the call or free its items. If
  copying fails, the original is converted (`ContainerArgumentCopy`).
- PySide reads a copy of a dict that another thread may change, such as a
  class dict or a dict passed to Qt or to `ClassInfo`. Without it the
  application crashes (`DictIterationSnapshot`).
- Routines added with `qAddPostRoutine()` from several threads all run at
  exit. Without it some are lost, or the process crashes (`PostRoutineBatch`).
- `Shiboken.dumpTree()` raises `NotImplementedError`.
- Under most locks of PySide no Python code runs, and a waiting thread stays
  attached. Where Python can run under a lock, in one-time initializations and
  in the dynamic types of Qt Remote Objects, a waiting thread detaches, as it
  does in CPython's own locks.

### Enums

- As in a GIL build, Qt's own enums become Python enum types when their module
  is imported.
  Python enums marked with `@QEnum` or `@QFlag` in a QObject class are
  registered with Qt as well. Plain Python enums are not affected.
- Classes with `@QEnum` can be defined in several threads at once, also while
  modules are imported. The enums of a class body are collected in the thread
  that runs it; without it the classes take each other's enums, or crash
  (`QEnumCollectorPerThread`). Enum types are created and converted under
  locks; without them the process hangs or aborts (`EnumPrivateLock`,
  `GenericEnumRegistryLock`).

### Signals and slots

- A method connected twice to the same signal gives two connections. Each
  disconnect removes one, and deleting the receiver removes both. In a GIL
  build the second disconnect fails with a `RuntimeWarning`, and one
  connection stays after the receiver is deleted and is still called
  (`RepeatedConnections`).
- A slot that is a method of an object without weak references, for example
  of a class with `__slots__` but no `__weakref__`, keeps that object alive
  until it is disconnected. In a GIL build the object can be freed while it is
  connected, and the next emit crashes. Other receivers are referenced weakly
  and held while a signal is delivered to them (`MethodReceiverUpgrade`).
- Once the deletion of a QObject is handed to its own thread, its signals raise
  the `RuntimeError` of a deleted object. For most QObjects this is as in a GIL
  build, where the object is deleted at once. A QWidget, QWindow or QQuickItem
  differs: a GIL build also deletes it later in the main thread, but its
  signals keep working until then (`SourceDeletionPending`).
- An emit, connect or disconnect that runs while another thread drops the last
  reference keeps the object until it is done (`SourceDeletionPending`).
- Signals and slots added at runtime from two threads are all kept; without it
  some are lost (`MetaObjectBuilderLock`).
- `connect()` and `disconnect()` can run in several threads at once; the table
  of connections is changed under a lock (`ConnectionHashLock`).
- A `Signal` attached to a class after the class was made gets its name once.
  Without it the first objects of the class, created at once in several
  threads, crash (`SignalNameOnce`).

### QML

- Two threads can create QML objects at once while the garbage collector
  runs. A construction holds no lock while Python code runs; without it the
  threads wait for each other and the application hangs (`QmlPlacementFree`).
- Only the QML type itself is placed in the memory QML reserved for it. A
  QObject that its `__init__()` creates is allocated as usual
  (`QmlPlacementType`).
- QML types can be registered in one thread while another thread uses Qt's
  meta types. Without it a meta type loses its meta object
  (`QObjectMetaTypeRecords`).

### Qt Remote Objects

- Dynamic types can be created in several threads at once; without it the
  application crashes (`RepRegistryLock`).
- A source property read while another thread sets it has either the old or
  the new value (`SourcePropertyCopy`).

## Limits

- Qt's thread rules apply as in C++. A QObject lives in its thread, and PySide
  adds no checks for using it from another one.
- Changing a PySide class, or a Python subclass of one, at runtime while other
  threads use it is not supported, for example assigning its attributes or its
  `__bases__`.
- A new QObject takes its signals from a list kept per class
  (`SignalClassList`). Writing into the dict of a class past `setattr()` leaves
  that list stale.
- That list holds no reference to the base classes. It relies on the
  free-threaded garbage collector stopping all other threads while it looks
  for dead classes.
- Not all shared state of the bindings is protected yet, for example the
  Python message handler of `qInstallMessageHandler()` and some caches that are
  filled on first use.
- A Qt message handler written in Python must not wait for a thread that reads
  a `RepFile` of QtRemoteObjects: Qt may call the handler while that thread
  holds the type registry.
- The last reference to a QObject owned by Python can go in any thread, for
  example in the garbage collector. PySide deletes the QObject in its own
  thread:
  - A QObject of the current thread, or of a thread that has finished, is
    deleted at once, as in a GIL build.
  - A QObject of another thread that still runs is handed to it with
    `deleteLater()`. It is deleted when that thread processes deferred
    deletions: in its event loop (`exec()`) or with
    `QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)`.
    `processEvents()` without a running event loop does not do it. A thread
    that never does, for example the main thread of a script without `exec()`,
    keeps the objects handed to it alive, also at exit.
  - A QWidget, QWindow or QQuickItem is deleted later in the main thread, as in
    a GIL build.
- `fork()` after the bindings were used is not supported: a lock that another
  thread held stays locked in the child. Use the `spawn` start method of
  `multiprocessing`, or `fork` followed by `exec`.
- Free-threaded builds need CPython 3.14.7t or a later 3.14t, since before
  3.14.7 interning a string can detach the thread. They were tested with
  3.14.10t only.

## Building and testing

- Build with a free-threaded interpreter and without the limited API, which
  `Python.h` of 3.14t rejects: `python3.14t setup.py build --limited-api=no`.
- Other bindings made with shiboken need `--module-uses-gil=auto`, or importing
  them switches the GIL back on; see
  [--module-uses-gil](../shiboken6/shibokengenerator.html#module-uses-gil).
  The option only declares the module free of the GIL: its C++ code and its
  callbacks into Python have to be thread-safe on their own.
- The tests run with `testrunner.py` as usual. Most free-threading tests are
  in `sources/pyside6/tests/pysidetest/` (`*_freethreading_test.py`,
  `state_lock_test.py`) and `sources/shiboken6/tests/samplebinding/`
  (`*_freethreading.py`).
- `PYSIDE6_OPTION_FT` selects the option bits: unset or `on` means all, `off`
  none, a number (also `0x` or `0b`) exactly its bits, `~` and a number all
  others. Only all bits set is supported; clearing bits is for testing.
- A counter-proof clears one bit and shows the defect the measure prevents.
  Every bit has one except `ModuleRegistryLock`, and `SignalClassList`, which
  only makes creating a QObject faster. Debug builds have failpoints, which stop
  a thread at a known place so that a second thread meets it there.
- `sources/pyside6/tests/manually/freethreading/README.md` explains how to run
  the counter-proofs and the failpoint tests.

## Changing the code

- Free-threaded code sits behind `#ifdef Py_GIL_DISABLED`, and the code of a
  GIL build stays as it was. `tools/gil_view.py --check` fails when a change
  alters code that a GIL build compiles.
- A new measure gets an option bit and a counter-proof, or a reason why none
  can exist. Code that only switches a feature off, such as lazy loading,
  needs no bit.

## Reference

- [Free-threaded builds](../shiboken6/libshiboken/freethreading.html): all
  option bits with their values
- [The state lock](../shiboken6/libshiboken/freethreading-statelock.html)
- [libpyside](../pyside6/libpyside/libpyside-module.html) and
  [libpysideremoteobjects](../pyside6/libpysideremoteobjects/libpysideremoteobjects-module.html)
- [PEP 703](https://peps.python.org/pep-0703/), which describes the
  free-threaded interpreter
