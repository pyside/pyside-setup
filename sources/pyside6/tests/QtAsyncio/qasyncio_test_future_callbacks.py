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


def context_with(value: str) -> contextvars.Context:
    context = contextvars.copy_context()
    context.run(VAR.set, value)
    return context


class QAsyncioTestCaseFutureCallbacks(unittest.TestCase):

    async def callback_contexts(self, outputs: list[str]):
        future = asyncio.get_running_loop().create_future()
        done = asyncio.Event()

        def callback(future):
            outputs.append(VAR.get())
            if len(outputs) == 2:
                done.set()

        future.add_done_callback(callback, context=context_with("first"))
        future.add_done_callback(callback, context=context_with("second"))
        future.set_result(None)
        await done.wait()

    def test_callback_runs_in_its_context(self):
        outputs_expected: list[str] = []
        outputs_real: list[str] = []

        asyncio.run(self.callback_contexts(outputs_expected))
        QtAsyncio.run(self.callback_contexts(outputs_real), keep_running=False)

        self.assertEqual(outputs_expected, ["first", "second"])
        self.assertEqual(outputs_real, outputs_expected)

    async def callback_entries(self, outputs: list[bool]):
        future = asyncio.get_running_loop().create_future()
        context = contextvars.copy_context()
        future.add_done_callback(print, context=context)
        # Third-party libraries such as anyio read _callbacks and expect
        # (callback, context) pairs, as stored by asyncio.Future.
        callback, stored_context = future._callbacks[0]
        outputs.append(callback is print)
        outputs.append(stored_context is context)
        future.remove_done_callback(print)
        outputs.append(len(future._callbacks) == 0)

    def test_callbacks_are_stored_with_context(self):
        outputs_real: list[bool] = []
        QtAsyncio.run(self.callback_entries(outputs_real), keep_running=False)
        self.assertEqual(outputs_real, [True, True, True])


if __name__ == "__main__":
    unittest.main()
