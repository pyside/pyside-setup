# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
# Qt-Security score:significant reason:default
from __future__ import annotations

from . import events
from . import futures
import traceback

from typing import Any, Optional

import asyncio
import collections.abc
import concurrent.futures
import contextvars


class QAsyncioTask(futures.QAsyncioFuture):
    """ https://docs.python.org/3/library/asyncio-task.html """

    def __init__(self, coro: collections.abc.Generator | collections.abc.Coroutine, *,
                 loop: "events.QAsyncioEventLoop | None" = None, name: str | None = None,
                 context: contextvars.Context | None = None) -> None:
        # Like asyncio.Task, each task runs in its own copy of the
        # context, so that ContextVar values do not leak between tasks.
        if context is None:
            context = contextvars.copy_context()
        super().__init__(loop=loop, context=context)
        self._source_traceback = None  # required for Python < 3.11

        self._state: futures.QAsyncioFuture.FutureState = futures.QAsyncioFuture.FutureState.PENDING
        self._exception: Optional[BaseException] = None

        self._coro = coro  # The coroutine for which this task was created.
        self._name = name if name else "QtTask"

        # The task creates a handle for its coroutine. The handle enqueues the
        # task's step function as its callback in the event loop.
        self._loop.call_soon(self._step, context=self._context)

        # The task step function executes the coroutine until it finishes,
        # raises an exception or returns a future. If a future was returned,
        # the task will await its completion (or exception). If the task is
        # cancelled while it awaits a future, this future must also be
        # cancelled in order for the cancellation to be successful.
        # The field names _fut_waiter and _must_cancel match asyncio.Task,
        # which third-party libraries such as anyio read.
        self._fut_waiter: asyncio.Future | None = None

        self._must_cancel = False  # PYSIDE-2644; see _step
        self._cancel_count = 0
        self._cancel_message: str | None = None
        # Store traceback in case of Exception. Useful when exception happens in coroutine
        self._tb: str | None = None

        # https://docs.python.org/3/library/asyncio-extending.html#task-lifetime-support
        asyncio._register_task(self)  # type: ignore[arg-type]

    def __repr__(self) -> str:
        state: str = "Unknown"
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

    def set_result(self, result: Any) -> None:  # type: ignore[override]
        # This function is not inherited from the Future APIs.
        raise QAsyncioTask.QtTaskApiMisuseError("Tasks cannot set results")

    def set_exception(self, exception: Any) -> None:  # type: ignore[override]
        # This function is not inherited from the Future APIs.
        raise QAsyncioTask.QtTaskApiMisuseError("Tasks cannot set exceptions")

    def _step(self,
              exception_or_future: BaseException | futures.QAsyncioFuture | None = None) -> None:
        """
        The step function is the heart of a task. It is scheduled in the event
        loop repeatedly, executing the coroutine "step" by "step" (i.e.,
        iterating through the asynchronous generator) until it finishes with an
        exception or successfully. Each step can optionally receive an
        exception or a future as a result from a previous step to handle.
        """

        if self.done():
            return
        result = None
        self._fut_waiter = None

        if self._must_cancel:
            exception_or_future = asyncio.CancelledError(self._cancel_message)
            self._must_cancel = False

        if asyncio.futures.isfuture(exception_or_future):
            try:
                exception_or_future.result()
            except BaseException as e:
                exception_or_future = e

        try:
            asyncio._enter_task(self._loop, self)  # type: ignore[arg-type]

            # It is at this point that the coroutine is resumed for the current
            # step (i.e. asynchronous generator iteration). It will now be
            # executed until it yields (and potentially returns a future),
            # raises an exception, is cancelled, or finishes successfully.

            if isinstance(exception_or_future, BaseException):
                # If the coroutine doesn't handle this exception, it propagates
                # to the caller.
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
            self._tb = traceback.format_exc()
        else:
            if asyncio.futures.isfuture(result):
                # If the coroutine yields a future, the task will await its
                # completion, and at that point the step function will be
                # called again.
                result.add_done_callback(
                    self._step, context=self._context)  # type: ignore[arg-type]

                # The task will await the completion (or exception) of this
                # future. If the task is cancelled while it awaits a future,
                # this future must also be cancelled.
                self._fut_waiter = result

                if self._must_cancel:
                    # PYSIDE-2644: If the task was cancelled at this step and a
                    # new future was created to be awaited, then it should be
                    # cancelled as well. Otherwise, in some scenarios like a
                    # loop inside the task and with bad timing, if the new
                    # future is not cancelled, the task would continue running
                    # in this loop despite having been cancelled. This bad
                    # timing can occur especially if the first future finishes
                    # very quickly.
                    if self._fut_waiter.cancel(self._cancel_message):
                        self._must_cancel = False
            elif result is None:
                # If no future was yielded, we schedule the step function again
                # without any arguments.
                self._loop.call_soon(self._step, context=self._context)
            else:
                # This is not supposed to happen.
                exception = RuntimeError(f"Bad task result: {result}")
                self._loop.call_soon(self._step, exception, context=self._context)
        finally:
            asyncio._leave_task(self._loop, self)  # type: ignore[arg-type]

            if self._exception and not self.cancelled():
                message = str(self._exception)
                if message == "None":
                    message = ""
                else:
                    message = "An exception occurred during task execution"
                self._loop.call_exception_handler({
                    "message": message,
                    "exception": self._exception,
                    "task": self,
                    "future": (exception_or_future
                               if asyncio.futures.isfuture(exception_or_future)
                               else None),
                    "traceback": self._tb
                })

            if self.done():
                self._schedule_callbacks()

                # https://docs.python.org/3/library/asyncio-extending.html#task-lifetime-support
                asyncio._unregister_task(self)  # type: ignore[arg-type]

    def get_stack(self, *, limit=None) -> list[Any]:
        # TODO
        raise NotImplementedError("QtTask.get_stack is not implemented")

    def print_stack(self, *, limit=None, file=None) -> None:
        # TODO
        raise NotImplementedError("QtTask.print_stack is not implemented")

    def get_coro(self) -> collections.abc.Generator | collections.abc.Coroutine:
        return self._coro

    def get_name(self) -> str:
        return self._name

    def set_name(self, value) -> None:
        self._name = str(value)

    def cancel(self, msg: str | None = None) -> bool:
        if self.done():
            return False
        self._cancel_count += 1
        self._cancel_message = msg
        # A task that is awaiting a future must also cancel this future in
        # order for the cancellation to be successful. If there is no such
        # future, or it is already done, cancel the coroutine at the next
        # step instead.
        if self._fut_waiter is not None and self._fut_waiter.cancel(msg):
            return True
        self._must_cancel = True  # PYSIDE-2644; see _step
        return True

    def uncancel(self) -> int:
        if self._cancel_count > 0:
            self._cancel_count -= 1
        return self._cancel_count

    def cancelling(self) -> int:
        return self._cancel_count
