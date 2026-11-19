# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

"""Test cases for QtAsyncio"""

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

import asyncio
import contextvars
import PySide6.QtAsyncio as QtAsyncio

VAR = contextvars.ContextVar("VAR", default="unset")


class QAsyncioTestCaseContextVars(unittest.TestCase):
    """Each task runs in its own copy of the context, as with asyncio.Task."""

    async def worker(self, name: str) -> str:
        VAR.set(name)
        # Let the other task run and set its own value in between.
        await asyncio.sleep(0.01)
        return VAR.get()

    async def main(self, outputs: list[str]):
        outputs.extend(await asyncio.gather(self.worker("A"), self.worker("B")))
        outputs.append(VAR.get())

    def test_contextvars_isolated_per_task(self):
        outputs_expected: list[str] = []
        outputs_real: list[str] = []

        asyncio.run(self.main(outputs_expected))
        QtAsyncio.run(self.main(outputs_real), keep_running=False)

        self.assertEqual(outputs_expected, ["A", "B", "unset"])
        self.assertEqual(outputs_real, outputs_expected)


if __name__ == "__main__":
    unittest.main()
