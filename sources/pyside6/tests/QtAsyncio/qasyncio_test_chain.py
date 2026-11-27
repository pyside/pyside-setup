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
import random
import time
from PySide6.QtAsyncio import QAsyncioEventLoopPolicy


class QAsyncioTestCaseChain(unittest.TestCase):

    async def link(self, output, n, i):
        t = random.randint(0, 5)
        output += f"link {i}({n}): {t}s "
        await asyncio.sleep(i)
        result = f"result {n}-{i}"
        output += f"link {i}({n}) finished with {result} "
        return result

    async def chain(self, output, n):
        link1 = await self.link(output, n, 0.2)
        link2 = await self.link(output, n, 0.5)
        output += f"chain {n}: {link1} -> {link2} "

    async def gather(self, output, *args):
        await asyncio.gather(*(self.chain(output, n) for n in args))

    def test_chain(self):
        args = [1, 2, 3]

        outputs_expected = []
        outputs_real = []

        # Run the code without QAsyncioEventLoopPolicy
        random.seed(17)
        asyncio.set_event_loop_policy(None)
        start = time.perf_counter()
        asyncio.run(self.gather(outputs_expected, *args))
        end_expected = time.perf_counter() - start

        # Run the code with QAsyncioEventLoopPolicy and QtEventLoop
        random.seed(17)
        asyncio.set_event_loop_policy(QAsyncioEventLoopPolicy())
        start = time.perf_counter()
        asyncio.run(self.gather(outputs_real, *args))
        end_real = time.perf_counter() - start

        self.assertEqual(outputs_expected, outputs_real)
        self.assertAlmostEqual(end_expected, end_real, delta=0.5)


if __name__ == '__main__':
    unittest.main()
