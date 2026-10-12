#!/usr/bin/env python
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""Every option bit has a counter-proof that names its defect.

  1. Every bit is cleared by a counter-proof, or listed in
     DEMONSTRATED_ELSEWHERE with the place that proves it.
  2. Every counter-proof clears a bit of the enum, names its defect
     (a constant `expect`) and proves an entry of TESTS.
  3. proof-* entries are the counter-proofs, the others the test_*
     functions.

    check_bit_coverage.py [sbkftoptions.h] [failpoints.py]

Exit 0 when all hold, 1 otherwise.
"""

import ast
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ftoptions  # noqa: E402

TESTS_FILE = HERE / "failpoints.py"

# Bits proven outside failpoints.py: bit -> the proof.
DEMONSTRATED_ELSEWHERE: dict[str, str] = {}


def tests_table(path: Path) -> tuple[dict, set]:
    """The TESTS mapping and the test_* functions, from the syntax tree."""
    tree = ast.parse(path.read_text())
    table, functions = {}, set()
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"):
            functions.add(node.name)
        if isinstance(node, ast.Assign) and any(
                getattr(target, "id", "") == "TESTS" for target in node.targets):
            for key, value in zip(node.value.keys, node.value.values):
                table[key.value] = value
    return table, functions


def counterproofs(node: ast.AST) -> list[dict]:
    """Every counterproof() call under node, by parameter name."""
    names = ("option", "test", "expect", "context")
    found = []
    for sub in ast.walk(node):
        if not isinstance(sub, ast.Call) or getattr(sub.func, "id", "") != "counterproof":
            continue
        call = {n: a.value if isinstance(a, ast.Constant) else None
                for n, a in zip(names, sub.args)}
        call.update({k.arg: k.value.value if isinstance(k.value, ast.Constant) else None
                     for k in sub.keywords})
        found.append(call)
    return found


def main() -> int:
    header = Path(sys.argv[1]) if len(sys.argv) > 1 else ftoptions.HEADER
    tests_path = Path(sys.argv[2]) if len(sys.argv) > 2 else TESTS_FILE
    bits = ftoptions.option_bits(header)
    tests, functions = tests_table(tests_path)

    cleared: dict[str, list[str]] = {}
    complaints = []
    for name, node in tests.items():
        for call in counterproofs(node):
            for option in (call.get("option"), call.get("context")):
                if option is not None:
                    cleared.setdefault(option, []).append(name)
            if call.get("test") not in tests:
                complaints.append(f"{name} proves {call.get('test')!r}, not an entry")
            if not call.get("expect"):
                complaints.append(f"{name} names no defect (expect)")

    print(f"{len(bits)} bits, {len(tests)} entries, {len(functions)} test functions")
    for bit in bits:
        if bit not in cleared and bit not in DEMONSTRATED_ELSEWHERE:
            complaints.append(f"{bit} = {bits[bit]:#x} has no counter-proof")
    for option, users in cleared.items():
        if option not in bits:
            complaints.append(f"{', '.join(users)} clears {option!r}, not in the enum")
    for bit in DEMONSTRATED_ELSEWHERE:
        if bit not in bits:
            complaints.append(f"{bit} is listed as proven elsewhere, not in the enum")

    proving = {n for n in tests if counterproofs(tests[n])}
    named = {n for n in tests if n.startswith("proof-")}
    complaints += [f"{n} is named proof-* but proves nothing" for n in sorted(named - proving)]
    complaints += [f"{n} proves something but is not named proof-*"
                   for n in sorted(proving - named)]
    if (len(tests) - len(proving)) != len(functions):
        complaints.append(f"{len(tests) - len(proving)} subjects, "
                          f"{len(functions)} test functions")

    for line in complaints:
        print(f"  {line}")
    if not complaints:
        print(f"  clean: {len(cleared)} bits by counter-proof, "
              f"{len(DEMONSTRATED_ELSEWHERE)} elsewhere")
    return 1 if complaints else 0


if __name__ == "__main__":
    sys.exit(main())
