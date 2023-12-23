# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

from .events import (
    QAsyncioEventLoopPolicy, QAsyncioEventLoop, QAsyncioHandle, QAsyncioTimerHandle
)
from .futures import QAsyncioFuture
from .tasks import QAsyncioTask

import asyncio
import typing

__all__ = [
    "QAsyncioEventLoopPolicy", "QAsyncioEventLoop",
    "QAsyncioHandle", "QAsyncioTimerHandle",
    "QAsyncioFuture", "QAsyncioTask"
]


def run(coro: typing.Optional[typing.Coroutine] = None,
        keep_running: typing.Optional[bool] = True, *,
        debug: typing.Optional[bool] = None) -> None:
    """Run the QtAsyncio event loop."""

    # Event loop policies are expected to be deprecated with Python 3.13, with
    # subsequent removal in Python 3.15. At that point, part of the current
    # logic of the QAsyncioEventLoopPolicy constructor will have to be moved
    # here and/or to a loop factory class (to be provided as an argument to
    # asyncio.run()), namely setting up the QCoreApplication and the SIGINT
    # handler.
    #
    # More details:
    # https://discuss.python.org/t/removing-the-asyncio-policy-system-asyncio-set-event-loop-policy-in-python-3-15/37553  # noqa: E501
    asyncio.set_event_loop_policy(QAsyncioEventLoopPolicy())

    if keep_running:
        if coro:
            asyncio.ensure_future(coro)
        asyncio.get_event_loop().run_forever()
    else:
        if coro:
            asyncio.run(coro, debug=debug)
        else:
            raise RuntimeError(
                "QtAsyncio was set to keep running after the coroutine "
                "finished, but no coroutine was provided.")
