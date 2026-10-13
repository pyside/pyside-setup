#!/usr/bin/env python
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""Machinery for the failpoint tests in failpoints.py.

Finding the build, arming a failpoint, running threads, and counterproof():
run a test again with one option bit cleared and require the defect itself.
"""

import os
import signal
import subprocess
import sys
import sysconfig
import threading
from pathlib import Path
from typing import Callable, NamedTuple

import ftoptions

REPO = Path(__file__).resolve().parents[5]
SELF = Path(__file__).resolve().parent / "failpoints.py"

SKIP = 77           # nothing ran; the matrix shows "skip"
HANG_RC = 124
# The longest child limit of the tests: a hanging test reports its own defect before
# its subject is cut, and the cut stays below the matrix's per-test budget
CHILD_LIMIT = 60
CHILD_TIMEOUT = max(CHILD_LIMIT + 30, int(os.environ.get("FAILPOINT_TIMEOUT", "300")) // 3)

OPTIONS = ftoptions.option_bits()


def build_dir() -> Path:
    if "BUILD_DIR" in os.environ:
        return Path(os.environ["BUILD_DIR"])
    history = REPO / "build_history"
    entries = sorted(history.glob("*/build_dir.txt"), key=lambda p: p.parent.name,
                     reverse=True)
    for entry in entries:
        build = Path(entry.read_text().splitlines()[0])
        if build.is_dir():
            return build
    sys.exit("no build found; set BUILD_DIR")


BUILD = build_dir()
sys.path.insert(0, os.fspath(BUILD.parent / "package_for_wheels"))
for _binding in ("samplebinding", "otherbinding", "minimalbinding", "smartbinding"):
    _path = BUILD / "shiboken6" / "tests" / _binding
    if _path.is_dir():
        sys.path.append(os.fspath(_path))

from shiboken6 import Shiboken  # noqa: E402

# The build, not the run: PYTHON_GIL=1 keeps the measures
FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
# Free-threaded debug builds without the limited API only
HAS_HOOKS = hasattr(Shiboken, "armFailpoint")


def gil_enabled() -> bool:
    return sys._is_gil_enabled() if hasattr(sys, "_is_gil_enabled") else True


def run_threads(targets, count: int = 1, timeout: float = 30.0) -> list[str]:
    """Run one callable on `count` threads, or each of a list on its own
    thread. Returns the exceptions raised and threads that did not return."""
    errors: list[str] = []
    funcs = list(targets) if isinstance(targets, (list, tuple)) else [targets] * count

    def wrapped(func: Callable[[], None]) -> None:
        try:
            func()
        except BaseException as exc:                # noqa: BLE001
            errors.append(repr(exc))

    threads = [threading.Thread(target=wrapped, args=(f,)) for f in funcs]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=timeout)
    if any(thread.is_alive() for thread in threads):
        errors.append("a thread did not come back")
    return errors


class FailpointMissing(Exception):
    """This build has no such point: a skip, not a failure."""


class Failpoint:
    """Arm a point, let a thread run into it, release it afterwards."""

    def __init__(self, name: str, timeout_ms: int = 5000):
        self.name = name
        self.timeout_ms = timeout_ms

    def __enter__(self):
        if not HAS_HOOKS or not Shiboken.armFailpoint(self.name, self.timeout_ms):
            raise FailpointMissing(self.name)
        return self

    def __exit__(self, *exc):
        Shiboken.releaseFailpoint(self.name)
        Shiboken.clearFailpoints()
        return False

    def wait_until_reached(self, timeout: float = 5.0) -> bool:
        """Whether a thread arrived; polls, the test is about ordering."""
        deadline = threading.Event()
        timer = threading.Timer(timeout, deadline.set)
        timer.start()
        try:
            while not deadline.is_set():
                if Shiboken.failpointReached(self.name):
                    return True
            return False
        finally:
            timer.cancel()

    def release(self) -> bool:
        return Shiboken.releaseFailpoint(self.name)


# ------------------------------------------------------------ counterproofs

def _run_without(cleared: list[str], test: str) -> tuple[int, str]:
    """Run one test in its own process with those bits cleared."""
    bits = 0
    for name in cleared:
        bits |= OPTIONS[name]
    env = dict(os.environ, PYSIDE6_OPTION_FT=f"~{bits:#x}", PYTHONFAULTHANDLER="1")
    env.pop("FAILPOINT_EXPECT_GIL", None)
    proc = subprocess.Popen([sys.executable, os.fspath(SELF), test], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=CHILD_TIMEOUT)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        out, _ = proc.communicate()
        rc = HANG_RC
    return rc, out


def _verdict(out: str, rc: int = 0) -> str:
    """The last line carrying FAIL or "skipped:", from that mark on."""
    if rc == HANG_RC:
        return f"timeout after {CHILD_TIMEOUT} s"
    lines = out.strip().splitlines()
    for line in reversed(lines):
        for mark in ("FAIL", "skipped:"):
            index = line.find(mark)
            if index >= 0:
                return line[index:].strip()
    return lines[-1].strip() if lines else ""


def defect_shown(expect: str | set[str], rc: int, out: str) -> bool:
    """Whether a failed run failed on the defect `expect` names.

    "crash" means killed by a signal, "hang" means it did not return; any
    other text has to appear in the output. Any other failure is not the
    defect. A set of markers counts when one of them shows.
    """
    if not isinstance(expect, str):
        return any(defect_shown(e, rc, out) for e in expect)
    if expect == "crash":
        return rc < 0
    if expect == "hang":
        return rc == HANG_RC
    return rc not in (0, SKIP) and expect in out


def counterproof(option: str, test: str, expect: str | set[str],
                 context: str = "", gil_expect: str = "", attempts: int = 1) -> str:
    """The test passes with `option` and fails on its defect without it.

    `expect` names the defect, or a set of the forms it takes (see
    defect_shown()), `gil_expect` the defect as it shows with the GIL
    enabled, where that differs. `context` names a bit cleared in both
    runs, for measures that hide each other. `attempts` is how often the
    run without `option` may pass before that counts, for a defect that
    needs a race to show; only a failure on the defect proves anything.
    """
    if not FREE_THREADED:
        return "skipped: the option bits exist only in a free-threaded build"
    if gil_expect and gil_enabled():
        expect = gil_expect
    cleared = [context] if context else []
    rc, out = _run_without(cleared, test)
    if rc == SKIP:
        return f"skipped: {test} did not run ({_verdict(out)[:60]})"
    if rc != 0:
        where = f" without {context}" if context else ""
        return f"FAIL: {test} does not pass{where}: {_verdict(out, rc)[:60]}"
    rc, out = _run_without(cleared + [option], test)
    for _ in range(attempts - 1):
        if rc != 0:
            break
        rc, out = _run_without(cleared + [option], test)
    without = option + (f" (and {context})" if context else "")
    if rc == SKIP:
        return f"skipped: {test} did not run ({_verdict(out)[:60]})"
    if rc == 0:
        if gil_enabled():
            # The GIL serializes the threads the defect needs
            return f"skipped: the GIL hides the defect ({test} passes without {without})"
        return f"FAIL: {test} still passes without {without}"
    shown = expect if isinstance(expect, str) else " or ".join(sorted(expect))
    if not defect_shown(expect, rc, out):
        return (f"FAIL: {test} fails without {without}, but not on {shown!r}: "
                f"rc {rc}, {_verdict(out, rc)[:60]}")
    return f"ok ({test} shows {shown!r} without {without})"


# ------------------------------------------------------------------- driver

class Test(NamedTuple):
    run: Callable[[], str]
    expect: str = "ok"      # what the matrix has to see


def main(tests: dict[str, Test]) -> int:
    if "--list" in sys.argv[1:]:
        for name, test in tests.items():
            print(f"{name}:{test.expect}")
        return 0

    # Asked after the import, which can turn the GIL back on
    expected = os.environ.get("FAILPOINT_EXPECT_GIL")
    if expected is not None and gil_enabled() != (expected == "1"):
        print(f"FAIL: asked for the GIL {'on' if expected == '1' else 'off'}, "
              f"this process has it {'on' if gil_enabled() else 'off'}")
        return 2

    print(f"build : {BUILD}")
    names = Shiboken.failpointNames() if HAS_HOOKS else None
    print(f"points: {names if names is not None else '(no hooks in this build)'}")
    print(f"GIL   : {'on' if gil_enabled() else 'off'}")
    print()
    if not HAS_HOOKS:
        print("skipped: no free-threading hooks in this build")
        return SKIP

    wanted = sys.argv[1:] or list(tests)
    failed = skipped = 0
    for name in wanted:
        if name not in tests:
            print(f"unknown test {name!r}; known: {' '.join(tests)}")
            return 2
        Shiboken.clearFailpoints()
        try:
            outcome = tests[name].run()
        except FailpointMissing as missing:
            outcome = f"skipped: this build has no failpoint {missing!s}"
        except Exception as exc:              # noqa: BLE001
            outcome = f"FAIL: raised {exc!r}"
        print(f"{name:28} {outcome}")
        failed += outcome.startswith("FAIL")
        skipped += outcome.startswith("skipped")
    print()
    print("all clean" if not failed else f"{failed} failed")
    if failed:
        return 1
    return SKIP if skipped == len(wanted) else 0
