#!/usr/bin/env python
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""Every failpoint name is registered, and every registered name is used.

An unregistered name cannot be armed, so its test skips; a registered name
without a call site makes a test wait out its timeout.

    check_failpoint_names.py [files...]      default: the runtime sources

Exit 0 when both sides agree, 1 when they do not.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REGISTRY = HERE / "sbkfailpoint.cpp"
ARRAY = re.compile(r"KnownFailpoints\s*=\s*\{(.*?)\};", re.DOTALL)
CALL = re.compile(r'Shiboken::failpoint\s*\(\s*"([^"]+)"\s*\)')
# A call the generator writes into generated code, its quotes escaped
EMITTED = re.compile(r'Shiboken::failpoint\s*\(\s*\\"([^"\\]+)\\"\s*\)')
LITERAL = re.compile(r'"([^"]+)"')
# Directories without shipped runtime code.
NOT_RUNTIME = {"tests", "doc", "shiboken6_generator"}


def registered() -> set[str]:
    text = REGISTRY.read_text(encoding="utf-8")
    return {name for body in ARRAY.findall(text) for name in LITERAL.findall(body)}


def called(files: list[Path]) -> dict[str, str]:
    used: dict[str, str] = {}
    for path in files:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for name in CALL.findall(line) + EMITTED.findall(line):
                used.setdefault(name, f"{path.name}:{number}")
    return used


def default_files() -> list[Path]:
    sources = HERE.parents[1]
    files = sorted(f for pattern in ("*.cpp", "*.h") for f in sources.rglob(pattern)
                   if not NOT_RUNTIME & set(f.relative_to(sources).parts))
    # The generator itself is no runtime code, but what it writes is
    generator = sources / "shiboken6_generator" / "generator" / "shiboken" / "cppgenerator.cpp"
    return files + [generator] if generator.is_file() else files


def main() -> int:
    files = [Path(a) for a in sys.argv[1:]] or default_files()
    known = registered()
    used = called(files)
    problems = [f'{where}: "{name}" is not registered'
                for name, where in sorted(used.items()) if name not in known]
    problems += [f'{REGISTRY.name}: "{name}" has no call site'
                 for name in sorted(known - set(used))]
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"clean: {len(known)} failpoint(s), each registered and reached "
          f"({len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
