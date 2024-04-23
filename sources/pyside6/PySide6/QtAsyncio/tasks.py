# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

from . import events
from . import futures

import asyncio
import collections.abc
import concurrent.futures
import contextvars
import typing


class QAsyncioTask(futures.QAsyncioFuture):
    """ https://docs.python.org/3/library/asyncio-task.html """

    def __init__(self, coro: typing.Union[collections.abc.Generator, collections.abc.Coroutine], *,
                 loop: typing.Optional["events.QAsyncioEventLoop"] = None,
                 name: typing.Optional[str] = None,
                 context: typing.Optional[contextvars.Context] = None) -> None:
        super().__init__(loop=loop, context=context)

        self._coro = coro
        self._name = name if name else "QtTask"

        self._handle = self._loop.call_soon(self._step, context=self._context)

        self._cancellation_requests = 0

        self._future_to_await: typing.Optional[asyncio.Future] = None
        self._cancel_message: typing.Optional[str] = None
        self._cancelled = False

        asyncio._register_task(self)  # type: ignore[arg-type]

    def __repr__(self) -> str:
        if self._state == futures.QAsyncioFuture.FutureState.PENDING:
            state = "Pending"
        elif self._state == futures.QAsyncioFuture.FutureState.DONE_WITH_RESULT:
            state = "Done"
        elif self._state == futures.QAsyncioFuture.FutureState.DONE_WITH_EXCEPTION:
            state = f"Done with exception ({repr(self._exception)})"
        elif self._state == futures.QAsyncioFuture.FutureState.CANCELLED:
            state = "Cancelled"

        return f"Task '{self.get_name()}' with state: {state}"

    class QtTaskApiMisuseError(Exception):
        pass

    def set_result(self, result: typing.Any) -> None:  # type: ignore[override]
        # This function is not inherited from the Future APIs.
        raise QAsyncioTask.QtTaskApiMisuseError("Tasks cannot set results")

    def set_exception(self, exception: typing.Any) -> None:  # type: ignore[override]
        # This function is not inherited from the Future APIs.
        raise QAsyncioTask.QtTaskApiMisuseError("Tasks cannot set exceptions")

    def _step(self,
              exception_or_future: typing.Union[
                  BaseException, futures.QAsyncioFuture, None] = None) -> None:
        if self.done():
            return
        result = None
        self._future_to_await = None

        if asyncio.futures.isfuture(exception_or_future):
            try:
                exception_or_future.result()
            except BaseException as e:
                exception_or_future = e

        try:
            asyncio._enter_task(self._loop, self)  # type: ignore[arg-type]
            if isinstance(exception_or_future, BaseException):
                result = self._coro.throw(exception_or_future)
            else:
                result = self._coro.send(None)
        except StopIteration as e:
            self._state = futures.QAsyncioFuture.FutureState.DONE_WITH_RESULT
            self._result = e.value
        except (concurrent.futures.CancelledError, asyncio.exceptions.CancelledError) as e:
            self._state = futures.QAsyncioFuture.FutureState.CANCELLED
            self._exception = e
        except BaseException as e:
            self._state = futures.QAsyncioFuture.FutureState.DONE_WITH_EXCEPTION
            self._exception = e
        else:
            if asyncio.futures.isfuture(result):
                result.add_done_callback(
                    self._step, context=self._context)  # type: ignore[arg-type]
                self._future_to_await = result
                if self._cancelled:
                    # If the task was cancelled, then a new future should be
                    # cancelled as well. Otherwise, in some scenarios like
                    # a loop inside the task and with bad timing, if the new
                    # future is not cancelled, the task would continue running
                    # in this loop despite having been cancelled. This bad
                    # timing can occur especially if the first future finishes
                    # very quickly.
                    self._future_to_await.cancel(self._cancel_message)
            elif result is None:
                self._loop.call_soon(self._step, context=self._context)
            else:
                exception = RuntimeError(f"Bad task result: {result}")
                self._loop.call_soon(self._step, exception, context=self._context)
        finally:
            asyncio._leave_task(self._loop, self)  # type: ignore[arg-type]
            if self._exception:
                self._loop.call_exception_handler({
                    "message": (str(self._exception) if self._exception
                                else "An exception occurred during task "
                                "execution"),
                    "exception": self._exception,
                    "task": self,
                    "future": (exception_or_future
                               if asyncio.futures.isfuture(exception_or_future)
                               else None)
                })
            if self.done():
                self._schedule_callbacks()
                asyncio._unregister_task(self)  # type: ignore[arg-type]

    def get_stack(self, *, limit=None) -> typing.List[typing.Any]:
        # TODO
        raise NotImplementedError("QtTask.get_stack is not implemented")

    def print_stack(self, *, limit=None, file=None) -> None:
        # TODO
        raise NotImplementedError("QtTask.print_stack is not implemented")

    def get_coro(self) -> typing.Union[collections.abc.Generator, collections.abc.Coroutine]:
        return self._coro

    def get_name(self) -> str:
        return self._name

    def set_name(self, value) -> None:
        self._name = str(value)

    def cancel(self, msg: typing.Optional[str] = None) -> bool:
        if self.done():
            return False
        self._cancel_message = msg
        self._handle.cancel()
        if self._future_to_await is not None:
            self._future_to_await.cancel(msg)
        self._cancelled = True
        return True

    def uncancel(self) -> None:
        # TODO
        raise NotImplementedError("QtTask.uncancel is not implemented")

    def cancelling(self) -> bool:
        # TODO
        raise NotImplementedError("QtTask.cancelling is not implemented")
