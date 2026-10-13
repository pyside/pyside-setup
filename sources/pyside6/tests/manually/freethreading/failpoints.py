#!/usr/bin/env python
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""Deterministic tests of the free-threaded build, and their counter-proofs.

    failpoints.py [test ...]      all tests, or the named ones
    failpoints.py --list          names and expected outcomes

A test returns "ok", "FAIL: ..." or "skipped: ...". A `proof-<name>` entry
runs its subject again with one option bit cleared and has to see the defect.
How to run them: README.md.
"""

import os
import re
import subprocess
import sys

from fpharness import (BUILD, FREE_THREADED, REPO, Shiboken, Test, counterproof,
                       defect_shown, gil_enabled, main)


def test_hooks() -> str:
    """The hooks refuse a name this build does not have."""
    name = "no-such-failpoint"
    if Shiboken.armFailpoint(name):
        return f"FAIL: armed {name!r}"
    if Shiboken.releaseFailpoint(name) or Shiboken.failpointReached(name):
        return f"FAIL: {name!r} reported as parked"
    if name in Shiboken.failpointNames().split(","):
        return f"FAIL: {name!r} listed"
    return "ok"


def test_defect_match() -> str:
    """counterproof() accepts only the named defect, not any failure."""
    cases = [("crash", -11, "", True), ("crash", 1, "FAIL: x", False),
             ("hang", 124, "", True), ("hang", -6, "", False),
             ("x", 1, "FAIL: x", True), ("x", 1, "FAIL: y", False),
             ("x", 0, "x", False), ("x", 77, "skipped: x", False),
             ({"x", "y"}, 1, "FAIL: y", True), ({"x", "y"}, 1, "FAIL: z", False),
             ({"x", "crash"}, -11, "", True), ({"x", "y"}, 0, "x y", False)]
    wrong = [c for c in cases if defect_shown(*c[:3]) != c[3]]
    return f"FAIL: {wrong}" if wrong else "ok"


def test_option_parse() -> str:
    """PYSIDE6_OPTION_FT takes 64-bit values: ~N clears exactly N."""
    if not FREE_THREADED:
        return "skipped: the option bits exist only in a free-threaded build"
    every = (1 << 64) - 1
    # The option is read once per process (a static const in sbkftoptions.cpp),
    # so each value needs a process of its own
    probe = ("import sys; sys.path.insert(0, sys.argv[1]); "
             "from shiboken6 import Shiboken; print(Shiboken.ftOptionFlags())")
    wrong = []
    for value, expected in (("~0x40000000", every & ~0x40000000),
                            ("~0x100000000", every & ~0x100000000),
                            ("on", every), ("off", 0)):
        proc = subprocess.run([sys.executable, "-c", probe,
                               os.fspath(BUILD.parent / "package_for_wheels")],
                              env=dict(os.environ, PYSIDE6_OPTION_FT=value),
                              capture_output=True, text=True, timeout=60)
        got = proc.stdout.strip()
        if got != str(expected):
            wrong.append(f"{value}: {got or proc.stderr[-100:]}")
    return f"FAIL: {'; '.join(wrong)}" if wrong else "ok"


TESTS_DIR = REPO / "sources" / "pyside6" / "tests"
SAMPLE_DIR = REPO / "sources" / "shiboken6" / "tests" / "samplebinding"


def run_unittest(path, *names: str) -> str:
    """A unittest file in its own process: "ok", or what failed."""
    proc = subprocess.run([sys.executable, os.fspath(path), *names], capture_output=True,
                          text=True, env=dict(os.environ, BUILD_DIR=os.fspath(BUILD)),
                          timeout=300)
    if proc.returncode == 0:
        # unittest exits 0 when every test was skipped: that is no evidence
        ran = re.search(r"^Ran (\d+) tests?", proc.stderr, re.MULTILINE)
        skipped = re.search(r"^OK \(skipped=(\d+)\)", proc.stderr, re.MULTILINE)
        if ran and skipped and skipped.group(1) == ran.group(1):
            return f"skipped: all {ran.group(1)} tests skipped"
        return "ok"
    if proc.returncode < 0:
        return f"FAIL: crashed (signal {-proc.returncode})"
    # Each failed method by name, with the message its assertion carries.
    failures = re.findall(r"^FAIL: (\w+)", proc.stderr, re.MULTILINE)
    messages = sorted(set(re.findall(r" : (.+)$", proc.stderr, re.MULTILINE))
                      | set(re.findall(r"^AssertionError: (.+)$", proc.stderr, re.MULTILINE)))
    return f"FAIL: {', '.join(failures) or proc.stderr[-200:]}: {'; '.join(messages)}"


def test_owner_thread_deletion() -> str:
    """A QObject is deleted in its own thread."""
    return run_unittest(TESTS_DIR / "pysidetest" / "owner_thread_deletion_test.py")


def test_state_lock() -> str:
    """gc on a worker while main reparents keeps the graph."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py", "StateLockTest")


def test_class_assign() -> str:
    """__class__ assignment to other C++ bases is refused."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "WrapperLifetimeTest.testClassWithOtherCppBases")


def test_invalidate_list() -> str:
    """An object dropped during invalidate() is not walked freed."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "WrapperLifetimeTest.testInvalidateListChanged")


def test_container_argument() -> str:
    """A list argument changed during the call is not seen."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "WrapperLifetimeTest.testContainerArgumentChanged")


def test_container_keyword() -> str:
    """A keyword list changed during the call is not seen."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "WrapperLifetimeTest.testContainerKeywordArgumentChanged")


def test_constructor_install() -> str:
    """Two threads run one base __init__: one is refused."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "ConstructorInstallTest.testBaseInitFromTwoThreads")


def test_second_init_mixin() -> str:
    """A second __init__ is refused before a mixin runs."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "ConstructorInstallTest.testSecondInitRunsNoMixin")


def test_second_init_two_bases() -> str:
    """Each base __init__ of two C++ bases is admitted once."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "ConstructorInstallTest.testSecondInitWithTwoCppBases")


def test_second_init_override() -> str:
    """A second __init__ is refused before childEvent()."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "ConstructorInstallTest.testSecondInitRunsNoOverride")


def test_second_init_application() -> str:
    """A second application __init__ is refused."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "ConstructorInstallTest.testSecondInitKeepsApplication")


def test_dict_first_access() -> str:
    """An attribute set during the first __dict__ access stays."""
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testDictFirstAccess")


def test_lookup_meets_dying_wrapper() -> str:
    """A lookup does not revive a dying wrapper."""
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testLookupMeetsDyingWrapper")


def run_type_state(method: str) -> str:
    """One method of pysidetest/type_state_freethreading_test.py."""
    return run_unittest(TESTS_DIR / "pysidetest" / "type_state_freethreading_test.py",
                        f"TypeStateFreeThreadedTest.{method}")


def test_hierarchy_snapshot() -> str:
    """A hierarchy walk sees one graph during an import."""
    return run_type_state("testHierarchyWalkSeesOneGraph")


def test_doc_per_thread() -> str:
    """A help text does not change another thread's __doc__."""
    return run_type_state("testDocPerThread")


def test_post_routines() -> str:
    """Post routines from several threads all run."""
    return run_type_state("testPostRoutinesFromThreads")


def test_dict_snapshot() -> str:
    """A dict changed during its conversion does not crash."""
    return run_type_state("testDictChangedDuringConversion")


def test_dict_metaobject_build() -> str:
    """A mixin changed while a subclass parses its dict."""
    return run_type_state("testBaseClassChangedDuringMetaObjectBuild")


def test_dict_qobject_init() -> str:
    """A class changed while an instance looks for signals."""
    return run_type_state("testClassChangedDuringQObjectInit")


def test_dict_enum_shortcut() -> str:
    """A class changed during a search for an old enum name."""
    return run_type_state("testClassChangedDuringEnumShortcutLookup")


def test_dict_classinfo() -> str:
    """A dict given to ClassInfo and changed meanwhile."""
    return run_type_state("testClassInfoDictChanged")


def test_dict_debug_format() -> str:
    """The debug formatters of a dict another thread changes."""
    return run_type_state("testDebugFormatOfChangingDict")


def test_dict_module_signatures() -> str:
    """A module dict changed while its signatures finish."""
    return run_type_state("testModuleDictChangedDuringSignatures")


def test_converter_registry() -> str:
    """Converter names added during imports are found."""
    return run_type_state("testConverterNamesDuringImport")


def test_conversion_list() -> str:
    """A conversion walk keeps its list during an import."""
    return run_type_state("testConversionsExtendedDuringImport")


def test_module_registry() -> str:
    """Module execs at once while names are looked up."""
    return run_type_state("testParallelModuleExecs")


def test_qenum_classes() -> str:
    """QEnum classes in several threads get their own enums."""
    return run_type_state("testQEnumClassesFromThreads")


def test_stream_helper() -> str:
    """First QDataStream reads in two threads make one helper."""
    return run_type_state("testFirstStreamFromTwoThreads")


def test_module_dir() -> str:
    """dir() of a module in its exec does not meet a missing table."""
    return run_type_state("testDirDuringModuleExec")


def test_import_waits() -> str:
    """An import waits for an exec in another thread."""
    return run_type_state("testImportWaitsForModuleExec")


def test_two_bindings() -> str:
    """First imports of two bindings initialize libshiboken once."""
    return run_type_state("testFirstImportsOfTwoBindings")


def test_module_dict() -> str:
    """A module seen during replaceModuleDict() keeps dict and class."""
    return run_type_state("testModuleSeenWhileItsDictIsReplaced")


def test_generic_enum() -> str:
    """QEnum enums appended from several threads: none is lost."""
    return run_type_state("testGenericEnumAppendsFromThreads")


def test_wrapper_walk() -> str:
    """A walk holds a wrapper another thread drops."""
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testWalkWhileAWrapperGoes")


def test_wrapper_dump() -> str:
    """dumpWrapperMap() while wrappers go reads no freed node."""
    if gil_enabled():
        return ("skipped: the failpoint parks inside a raw lock, which must not "
                "detach; with the GIL enabled the second thread cannot run")
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testDumpWhileWrappersGo")


def test_main_queue() -> str:
    """Widgets queued during the main thread run are deleted once."""
    return run_unittest(TESTS_DIR / "pysidetest" / "state_lock_test.py",
                        "WrapperLifetimeTest.testMainThreadQueueGrowsWhileItRuns")


def test_mi_offsets() -> str:
    """First objects with two C++ bases get correct offsets."""
    if gil_enabled():
        return ("skipped: the failpoint parks inside a C++ static initializer, "
                "which cannot detach; without a failpoint no Python runs there")
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testFirstMultipleInheritanceFromTwoThreads")


def test_metatype_hash() -> str:
    """Metatypes read during a QML registration keep their meta objects."""
    return run_type_state("testMetaTypeReadDuringRegistration")


def test_mro_snapshot() -> str:
    """__bases__ assigned during an override lookup: the walk stays valid."""
    return run_type_state("testMroWalkSurvivesBasesSwap")


def test_bases_snapshot() -> str:
    """__bases__ assigned during a property lookup: the walk stays valid."""
    return run_type_state("testBasesWalkSurvivesBasesSwap")


def test_one_wrapper_per_address() -> str:
    """Two threads converting one C++ object get one wrapper."""
    if gil_enabled():
        return ("skipped: the failpoint parks inside a raw lock, which must not "
                "detach; with the GIL enabled the second thread cannot run")
    return run_unittest(SAMPLE_DIR / "wrapper_freethreading.py",
                        "WrapperFreeThreadedTest.testTwoThreadsConvertOneObject")


TESTS = {
    "hooks": Test(test_hooks),
    "defect-match": Test(test_defect_match),
    "option-parse": Test(test_option_parse),
    "owner-thread-deletion": Test(test_owner_thread_deletion),
    "proof-owner-thread-deletion": Test(lambda: counterproof(
        "OwnerThreadDeletion", "owner-thread-deletion", "not destroyed in the owner thread")),
    "proof-finished-owner-cleanup": Test(lambda: counterproof(
        "FinishedOwnerCleanup", "owner-thread-deletion",
        "never destroyed after its owner thread finished")),
    "state-lock": Test(test_state_lock),
    "proof-state-lock": Test(lambda: counterproof(
        "StateLock", "state-lock", "parent/child graph broke")),
    "constructor-install": Test(test_constructor_install),
    "proof-constructor-install": Test(lambda: counterproof(
        "ConstructorInstallOnce", "constructor-install", "one object was initialized twice")),
    "second-init-mixin": Test(test_second_init_mixin),
    "proof-second-init-mixin": Test(lambda: counterproof(
        "ConstructorInstallOnce", "second-init-mixin",
        "a refused __init__ ran the mixin __init__")),
    "second-init-two-bases": Test(test_second_init_two_bases),
    "proof-second-init-two-bases": Test(lambda: counterproof(
        "ConstructorInstallOnce", "second-init-two-bases",
        "a refused __init__ of one of two C++ bases built an object")),
    "second-init-override": Test(test_second_init_override),
    "proof-second-init-override": Test(lambda: counterproof(
        "ConstructorInstallOnce", "second-init-override",
        "a refused __init__ ran a Python override")),
    "second-init-application": Test(test_second_init_application),
    "proof-second-init-application": Test(lambda: counterproof(
        "ConstructorInstallOnce", "second-init-application",
        "a refused __init__ replaced the application")),
    "class-assign": Test(test_class_assign),
    "proof-class-assign": Test(lambda: counterproof(
        "ClassAssignCheck", "class-assign", "a C++ base changed under a wrapper")),
    "invalidate-list": Test(test_invalidate_list),
    "proof-invalidate-list": Test(lambda: counterproof(
        "InvalidateRootsOwned", "invalidate-list", "invalidate walked a freed object")),
    "container-argument": Test(test_container_argument),
    "proof-container-argument": Test(lambda: counterproof(
        "ContainerArgumentCopy", "container-argument",
        "a list argument changed during the call")),
    "container-keyword": Test(test_container_keyword),
    "proof-container-keyword": Test(lambda: counterproof(
        "ContainerArgumentCopy", "container-keyword",
        "a keyword list argument changed during the call")),
    "dict-first-access": Test(test_dict_first_access),
    "proof-dict-first-access": Test(lambda: counterproof(
        "DictPublishOnce", "dict-first-access", "the instance dict was replaced")),
    "lookup-meets-dying-wrapper": Test(test_lookup_meets_dying_wrapper),
    "proof-lookup-meets-dying-wrapper": Test(lambda: counterproof(
        "OwnedWrapperLookup", "lookup-meets-dying-wrapper", "crashed")),
    "one-wrapper-per-address": Test(test_one_wrapper_per_address),
    "proof-one-wrapper-per-address": Test(lambda: counterproof(
        "OneWrapperPerAddress", "one-wrapper-per-address", "one C++ object got two wrappers")),
    "hierarchy-snapshot": Test(test_hierarchy_snapshot),
    "proof-hierarchy-snapshot": Test(lambda: counterproof(
        "ClassHierarchySnapshot", "hierarchy-snapshot", "the walk saw two graphs")),
    "doc-per-thread": Test(test_doc_per_thread),
    "proof-doc-per-thread": Test(lambda: counterproof(
        "DocRecursionPerThread", "doc-per-thread", "__doc__ came from the raw descriptor")),
    "post-routines": Test(test_post_routines),
    "proof-post-routines": Test(lambda: counterproof(
        "PostRoutineBatch", "post-routines", "post routines lost")),
    "dict-snapshot": Test(test_dict_snapshot),
    # The freed list crashes, or is read as a shorter one: an IndexError
    "proof-dict-snapshot": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-snapshot", {"crashed", "IndexError"})),
    "dict-metaobject-build": Test(test_dict_metaobject_build),
    "proof-dict-metaobject-build": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-metaobject-build", {"crash", "crashed"})),
    "dict-qobject-init": Test(test_dict_qobject_init),
    "proof-dict-qobject-init": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-qobject-init", {"crash", "crashed"})),
    "dict-enum-shortcut": Test(test_dict_enum_shortcut),
    "proof-dict-enum-shortcut": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-enum-shortcut", {"crash", "crashed"})),
    "dict-classinfo": Test(test_dict_classinfo),
    "proof-dict-classinfo": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-classinfo", {"crash", "crashed"})),
    "dict-debug-format": Test(test_dict_debug_format),
    "proof-dict-debug-format": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-debug-format", {"crash", "crashed"})),
    "dict-module-signatures": Test(test_dict_module_signatures),
    "proof-dict-module-signatures": Test(lambda: counterproof(
        "DictIterationSnapshot", "dict-module-signatures", {"crash", "crashed"})),
    "converter-registry": Test(test_converter_registry),
    "proof-converter-registry": Test(lambda: counterproof(
        "ConverterRegistryLock", "converter-registry", "the converter registry broke")),
    "conversion-list": Test(test_conversion_list),
    "proof-conversion-list": Test(lambda: counterproof(
        "ConverterRegistryLock", "conversion-list", "a conversion walk lost its list")),
    "module-registry": Test(test_module_registry),
    "module-dir": Test(test_module_dir),
    "proof-module-dir": Test(lambda: counterproof(
        "ModuleDirDefault", "module-dir", "dir() met a module before its tables")),
    "import-waits": Test(test_import_waits),
    "two-bindings": Test(test_two_bindings),
    "proof-two-bindings": Test(lambda: counterproof(
        "FirstUseOnce", "two-bindings", "two first imports hung")),
    "proof-import-waits": Test(lambda: counterproof(
        "ModuleImportWaits", "import-waits", "a module was taken half initialized")),
    "module-dict": Test(test_module_dict),
    "proof-module-dict": Test(lambda: counterproof(
        "ModuleDictKept", "module-dict", "a module was seen between the two stores")),
    "qenum-classes": Test(test_qenum_classes),
    "proof-qenum-collector": Test(lambda: counterproof(
        "QEnumCollectorPerThread", "qenum-classes", "the QEnum registration broke")),
    "proof-enum-private": Test(lambda: counterproof(
        "EnumPrivateLock", "qenum-classes", "the QEnum registration broke")),
    "generic-enum": Test(test_generic_enum),
    "wrapper-walk": Test(test_wrapper_walk),
    "proof-wrapper-walk": Test(lambda: counterproof(
        "WrapperWalkOwned", "wrapper-walk", "the walk took a wrapper that was gone")),
    "wrapper-dump": Test(test_wrapper_dump),
    "proof-wrapper-dump": Test(lambda: counterproof(
        "WrapperDumpLocked", "wrapper-dump", "the wrapper map changed under its dump")),
    "main-queue": Test(test_main_queue),
    "proof-main-queue": Test(lambda: counterproof(
        "MainThreadQueueLock", "main-queue", "the main-thread queue changed while it ran")),
    "mi-offsets": Test(test_mi_offsets),
    "proof-mi-offsets": Test(lambda: counterproof(
        "MiOffsetsOnce", "mi-offsets", "the base offsets were read half computed")),
    "proof-generic-enum": Test(lambda: counterproof(
        "GenericEnumRegistryLock", "generic-enum", "the generic enum registry lost an append")),
    "stream-helper": Test(test_stream_helper),
    "proof-stream-helper": Test(lambda: counterproof(
        "StreamHelperOnce", "stream-helper", "two stream helpers")),
    "metatype-hash": Test(test_metatype_hash),
    "proof-metatype-hash": Test(lambda: counterproof(
        "QObjectMetaTypeRecords", "metatype-hash", "a metatype lost its meta object")),
    "mro-snapshot": Test(test_mro_snapshot),
    "proof-mro-snapshot": Test(lambda: counterproof(
        "MroSnapshot", "mro-snapshot", "the walk left the mro it started on")),
    "bases-snapshot": Test(test_bases_snapshot),
    "proof-bases-snapshot": Test(lambda: counterproof(
        "MroSnapshot", "bases-snapshot", "the walk left the bases it started on")),
}


if __name__ == "__main__":
    sys.exit(main(TESTS))
