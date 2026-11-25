# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

"""
testing/__init__.py

- install an alternative, flushing print
- define command.main as entry point
"""

import builtins

from . import command

main = command.main

# modify print so that it always flushes
builtins.orig_print = builtins.print


def print_flushed(*args, **kw):
    kw.setdefault("flush", True)
    builtins.orig_print(*args, **kw)


builtins.print = print_flushed

# We also could use "python -u" to get unbuffered output.
# This method is better since it needs no change of the interface.

# eof
