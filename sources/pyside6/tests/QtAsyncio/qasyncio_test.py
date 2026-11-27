# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

'''Test cases for QtAsyncio'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

import asyncio
from PySide6.QtAsyncio import QAsyncioEventLoopPolicy


class QAsyncioTestCase(unittest.TestCase):
    async def sleep(self, output):
        output += "Hello"
        await asyncio.sleep(0.2)
        output += "World"

    async def gather(self, output):
        await asyncio.gather(self.sleep(output), self.sleep(output), self.sleep(output))

    def test_sleep(self):
        outputs_expected = []
        outputs_real = []

        # Run the code without QAsyncioEventLoopPolicy
        asyncio.set_event_loop_policy(None)
        asyncio.run(self.sleep(outputs_expected))

        # Run the code with QAsyncioEventLoopPolicy and QtEventLoop
        asyncio.set_event_loop_policy(QAsyncioEventLoopPolicy())
        asyncio.run(self.sleep(outputs_real))

        self.assertEqual(outputs_expected, outputs_real)

    def test_gather(self):
        outputs_expected = []
        outputs_real = []

        # Run the code without QAsyncioEventLoopPolicy
        asyncio.set_event_loop_policy(None)
        asyncio.run(self.gather(outputs_expected))

        # Run the code with QAsyncioEventLoopPolicy and QtEventLoop
        asyncio.set_event_loop_policy(QAsyncioEventLoopPolicy())
        asyncio.run(self.gather(outputs_real))

        self.assertEqual(outputs_expected, outputs_real)


if __name__ == '__main__':
    unittest.main()
