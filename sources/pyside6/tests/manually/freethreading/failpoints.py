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

from fpharness import BUILD, FREE_THREADED, REPO, Shiboken, Test, counterproof, defect_shown, main


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


def test_owner_thread_deletion() -> str:
    """A Python-owned QObject is destroyed in its own thread when its wrapper
    dies elsewhere (pysidetest/owner_thread_deletion_test.py)."""
    test = REPO / "sources" / "pyside6" / "tests" / "pysidetest" / "owner_thread_deletion_test.py"
    proc = subprocess.run([sys.executable, os.fspath(test)], capture_output=True, text=True,
                          env=dict(os.environ, BUILD_DIR=os.fspath(BUILD)), timeout=300)
    if proc.returncode == 0:
        return "ok"
    # Each failed method by name, with the message its assertion carries.
    failures = re.findall(r"^FAIL: (\w+)", proc.stderr, re.MULTILINE)
    messages = sorted(set(re.findall(r" : (.+)$", proc.stderr, re.MULTILINE)))
    return f"FAIL: {', '.join(failures) or proc.stderr[-200:]}: {'; '.join(messages)}"


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
}


if __name__ == "__main__":
    sys.exit(main(TESTS))
