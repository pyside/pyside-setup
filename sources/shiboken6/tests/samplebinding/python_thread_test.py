# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

#!/usr/bin/env python

'''Tests for using Shiboken-based bindings with python threads'''

import contextlib
import logging
import os
from random import random
import sys
import threading
import time
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

import sample

# Bucket is not thread-safe: without a GIL, its two threads take turns
BUCKET_LOCK = (contextlib.nullcontext() if getattr(sys, "_is_gil_enabled", lambda: True)()
               else threading.Lock())

#logging.basicConfig(level=logging.DEBUG)


class Producer(threading.Thread):
    '''Producer thread'''

    def __init__(self, bucket, max_runs, *args):
        #Constructor. Receives the bucket
        super(Producer, self).__init__(*args)
        self.runs = 0
        self.bucket = bucket
        self.max_runs = max_runs
        self.production_list = []

    def run(self):
        while self.runs < self.max_runs:
            value = int(random() * 10) % 10
            with BUCKET_LOCK:
                self.bucket.push(value)
            self.production_list.append(value)
            logging.debug(f'PRODUCER - pushed {value}')
            self.runs += 1
            #self.msleep(5)
            time.sleep(0.01)


class Consumer(threading.Thread):
    '''Consumer thread'''
    def __init__(self, bucket, max_runs, *args):
        #Constructor. Receives the bucket
        super(Consumer, self).__init__(*args)
        self.runs = 0
        self.bucket = bucket
        self.max_runs = max_runs
        self.consumption_list = []

    def run(self):
        while self.runs < self.max_runs:
            with BUCKET_LOCK:
                value = None if self.bucket.empty() else self.bucket.pop()
            if value is not None:
                self.consumption_list.append(value)
                logging.debug(f'CONSUMER - got {value}')
                self.runs += 1
            else:
                logging.debug('CONSUMER - empty bucket')
            time.sleep(0.01)


class ProducerConsumer(unittest.TestCase):
    '''Basic test case for producer-consumer QThread'''

    def finishCb(self):
        #Quits the application
        self.app.exit(0)

    def testProdCon(self):
        #QThread producer-consumer example
        bucket = sample.Bucket()
        prod = Producer(bucket, 10)
        cons = Consumer(bucket, 10)

        prod.start()
        cons.start()

        #QObject.connect(prod, SIGNAL('finished()'), self.finishCb)
        #QObject.connect(cons, SIGNAL('finished()'), self.finishCb)

        prod.join()
        cons.join()

        self.assertEqual(prod.production_list, cons.consumption_list)


if __name__ == '__main__':
    unittest.main()
