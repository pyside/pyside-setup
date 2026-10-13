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
             ("x", 0, "x", False), ("x", 77, "skipped: x", False)]
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
    messages = sorted(set(re.findall(r" : (.+)$", proc.stderr, re.MULTILINE)))
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
}


if __name__ == "__main__":
    sys.exit(main(TESTS))
