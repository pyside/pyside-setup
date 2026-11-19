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
import PySide6.QtAsyncio as QtAsyncio


class QAsyncioTestCaseCancelState(unittest.TestCase):
    """
    Third-party libraries such as anyio read the _fut_waiter and
    _must_cancel fields of asyncio.Task to track cancellation. A QtAsyncio
    task must keep them in the same state as an asyncio.Task does.
    """

    async def cancel_while_waiting(self, outputs: list):
        future = asyncio.get_running_loop().create_future()

        async def worker():
            try:
                await future
            except asyncio.CancelledError:
                outputs.append("cancelled")

        task = asyncio.ensure_future(worker())
        await asyncio.sleep(0)
        outputs.append(task._fut_waiter is future)
        outputs.append(task._must_cancel)
        task.cancel()
        # The awaited future takes the cancellation, the task does not.
        outputs.append(future.cancelled())
        outputs.append(task._must_cancel)
        await task
        outputs.append(task._fut_waiter is None)

    async def cancel_while_running(self, outputs: list):
        async def worker():
            task = asyncio.current_task()
            outputs.append(task._fut_waiter is None)
            task.cancel()
            # Nothing is awaited, so the task must throw the cancellation
            # into the coroutine at its next await.
            outputs.append(task._must_cancel)
            try:
                await asyncio.sleep(1)
            except asyncio.CancelledError:
                outputs.append("cancelled")
            outputs.append(task._must_cancel)

        await asyncio.ensure_future(worker())

    def compare(self, scenario):
        outputs_expected: list = []
        outputs_real: list = []
        asyncio.run(scenario(outputs_expected))
        QtAsyncio.run(scenario(outputs_real), keep_running=False)
        self.assertEqual(outputs_real, outputs_expected)

    def test_cancel_while_waiting(self):
        self.compare(self.cancel_while_waiting)

    def test_cancel_while_running(self):
        self.compare(self.cancel_while_running)


if __name__ == "__main__":
    unittest.main()
