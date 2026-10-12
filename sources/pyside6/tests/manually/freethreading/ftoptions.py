#!/usr/bin/env python
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""The bits of PYSIDE6_OPTION_FT, read out of sbkftoptions.h, the one inventory."""

import re
import sys
from pathlib import Path

HEADER = (Path(__file__).resolve().parents[5] / "sources" / "shiboken6"
          / "libshiboken" / "sbkftoptions.h")


def option_bits(header: Path = HEADER) -> dict[str, int]:
    """Every name in `enum class Option`, with its value."""
    body = header.read_text()
    start = body.find("enum class Option")
    end = body.find("};", start)
    if start < 0 or end < 0:
        sys.exit(f"no enum class Option in {header}")
    found = re.findall(r"^\s*(\w+)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*,?",
                       body[start:end], re.MULTILINE)
    bits = {name: int(value, 0) for name, value in found}
    # Powers of two from 1 up: a line the pattern missed shows here
    values = sorted(bits.values())
    if values != [1 << i for i in range(len(values))]:
        sys.exit(f"option bits in {header} are not 1, 2, 4 ...: "
                 f"{[hex(v) for v in values]}")
    return bits


if __name__ == "__main__":
    for name, bit in option_bits().items():
        print(f"{name:28} {bit:#010x}")
