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


class QAsyncioTestCaseQueues(unittest.TestCase):

    async def produce(self, output, queue):
        for _ in range(random.randint(0, 2)):
            await asyncio.sleep(random.random())
            await queue.put(self.i)
            output += f"{self.i} added to queue\n"
            self.i += 1

    async def consume(self, output, queue):
        while True:
            await asyncio.sleep(random.random())
            i = await queue.get()
            output += f"{i} pulled from queue\n"
            queue.task_done()

    async def main(self, output1, output2, num_producers, num_consumers):
        self.i = 0
        queue = asyncio.Queue()
        producers = [
            asyncio.create_task(self.produce(output1, queue)) for _ in range(num_producers)]
        consumers = [
            asyncio.create_task(self.consume(output2, queue)) for _ in range(num_consumers)]
        await asyncio.gather(*producers)
        await queue.join()
        for consumer in consumers:
            consumer.cancel()

    def test_queues(self):
        args = [(2, 3), (2, 1)]
        for arg in args:
            outputs_expected1 = []
            outputs_expected2 = []
            outputs_real1 = []
            outputs_real2 = []

            # Run the code without QAsyncioEventLoopPolicy
            random.seed(17)
            start = time.perf_counter()
            asyncio.set_event_loop_policy(None)
            asyncio.run(self.main(outputs_expected1, outputs_expected2, *arg))
            end_expected = time.perf_counter() - start

            # Run the code with QAsyncioEventLoopPolicy and QtEventLoop
            random.seed(17)
            start = time.perf_counter()
            asyncio.set_event_loop_policy(QAsyncioEventLoopPolicy())
            asyncio.run(self.main(outputs_real1, outputs_real2, *arg))
            end_real = time.perf_counter() - start

            self.assertEqual(outputs_expected1, outputs_real1)
            self.assertEqual(outputs_expected2, outputs_real2)
            self.assertAlmostEqual(end_expected, end_real, delta=1)


if __name__ == "__main__":
    unittest.main()
