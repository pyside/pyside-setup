# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
# Qt-Security score:significant reason:default
from __future__ import annotations

from . import events

from typing import Any, Callable

import asyncio
import contextvars
import enum


class QAsyncioFuture():
    """ https://docs.python.org/3/library/asyncio-future.html """

    # Declare that this class implements the Future protocol. The field must
    # exist and be boolean - True indicates 'await' or 'yield from', False
    # indicates 'yield'.
    _asyncio_future_blocking = False

    class FutureState(enum.Enum):
        PENDING = enum.auto()
        CANCELLED = enum.auto()
        DONE_WITH_RESULT = enum.auto()
        DONE_WITH_EXCEPTION = enum.auto()

    def __init__(self, *, loop: "events.QAsyncioEventLoop | None" = None,
                 context: contextvars.Context | None = None) -> None:
        self._loop: "events.QAsyncioEventLoop"
        if loop is None:
            self._loop = asyncio.events.get_event_loop()  # type: ignore[assignment]
        else:
            self._loop = loop
        self._context = context

        self._state = QAsyncioFuture.FutureState.PENDING
        self._result: Any = None
        self._exception: BaseException | None = None

        self._cancel_message: str | None = None

        # Callbacks that are called when the future is done, each with the
        # context to run it in. The (callback, context) pairs match
        # asyncio.Future, which third-party libraries such as anyio read.
        self._callbacks: list[tuple[Callable, contextvars.Context]] = list()

    def __await__(self):
        if not self.done():
            self._asyncio_future_blocking = True
            yield self
            if not self.done():
                raise RuntimeError("await was not used with a Future or Future-like object")
        return self.result()

    __iter__ = __await__

    def _schedule_callbacks(self):
        """ A future can optionally have callbacks that are called when the future is done. """
        for cb, context in self._callbacks:
            self._loop.call_soon(cb, self, context=context)

    def result(self) -> Any | Exception:
        if self._state == QAsyncioFuture.FutureState.DONE_WITH_RESULT:
            return self._result
        if self._state == QAsyncioFuture.FutureState.DONE_WITH_EXCEPTION and self._exception:
            raise self._exception
        if self._state == QAsyncioFuture.FutureState.CANCELLED:
            if self._cancel_message:
                raise asyncio.CancelledError(self._cancel_message)
            else:
                raise asyncio.CancelledError
        raise asyncio.InvalidStateError

    def set_result(self, result: Any) -> None:
        self._result = result
        self._state = QAsyncioFuture.FutureState.DONE_WITH_RESULT
        self._schedule_callbacks()

    def set_exception(self, exception: Exception) -> None:
        self._exception = exception
        self._state = QAsyncioFuture.FutureState.DONE_WITH_EXCEPTION
        self._schedule_callbacks()

    def done(self) -> bool:
        return self._state != QAsyncioFuture.FutureState.PENDING

    def cancelled(self) -> bool:
        return self._state == QAsyncioFuture.FutureState.CANCELLED

    def add_done_callback(self, cb: Callable, *,
                          context: contextvars.Context | None = None) -> None:
        if context is None:
            context = contextvars.copy_context()
        if self.done():
            self._loop.call_soon(cb, self, context=context)
        else:
            self._callbacks.append((cb, context))

    def remove_done_callback(self, cb: Callable) -> int:
        original_len = len(self._callbacks)
        self._callbacks = [(_cb, _context) for _cb, _context in self._callbacks
                           if _cb != cb]
        return original_len - len(self._callbacks)

    def cancel(self, msg: str | None = None) -> bool:
        if self.done():
            return False
        self._state = QAsyncioFuture.FutureState.CANCELLED
        self._cancel_message = msg
        self._schedule_callbacks()
        return True

    def exception(self) -> BaseException | None:
        if self._state == QAsyncioFuture.FutureState.CANCELLED:
            raise asyncio.CancelledError
        if self.done():
            return self._exception
        raise asyncio.InvalidStateError

    def get_loop(self) -> asyncio.AbstractEventLoop:
        return self._loop
