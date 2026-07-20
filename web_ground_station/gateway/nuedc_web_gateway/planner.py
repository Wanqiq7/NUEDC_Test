import asyncio
import json
import os
import selectors
import signal
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Mapping

from .models import PlanningRequest


_MAX_INPUT_BYTES = 64 * 1024


class PlannerError(RuntimeError):
    def __init__(self, error_code: str, message: str) -> None:
        super().__init__(message)
        self.error_code = error_code


class PlannerClient:
    def __init__(
        self,
        executable: Path,
        *,
        timeout_s: float,
        max_output_bytes: int,
    ) -> None:
        self._executable = executable
        self._timeout_s = timeout_s
        self._max_output_bytes = max_output_bytes

    async def plan(self, request: PlanningRequest) -> dict[str, Any]:
        request_bytes = json.dumps(
            {
                "schema": "h_planning_request_v1",
                "case_path": request.case_path,
                "no_fly_cells": request.no_fly_cells,
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        if len(request_bytes) > _MAX_INPUT_BYTES:
            raise PlannerError(
                "planner_input_too_large",
                "planner input exceeds 64 KiB",
            )

        planner_task = asyncio.create_task(
            asyncio.to_thread(self._run_planner, request_bytes)
        )
        # Some runtimes miss the executor wakeup after Popen; the timer keeps the
        # event loop progressing without running subprocess work on this thread.
        while not planner_task.done():
            await asyncio.sleep(0.005)
        stdout = planner_task.result()

        try:
            response = json.loads(stdout)
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise PlannerError(
                "planner_invalid_response",
                "planner returned invalid JSON",
            ) from error

        if not isinstance(response, dict) or response.get("ok") is not True:
            raise PlannerError(
                "planner_invalid_response",
                "planner response is not successful",
            )

        plan = response.get("plan")
        if not _is_canonical_plan(plan):
            raise PlannerError(
                "planner_invalid_response",
                "planner response does not contain a canonical task plan",
            )
        return plan

    def _run_planner(self, request_bytes: bytes) -> bytes:
        try:
            process = subprocess.Popen(
                [str(self._executable)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                start_new_session=os.name == "posix",
            )
        except OSError as error:
            raise PlannerError(
                "planner_process_failed",
                f"planner process could not start: {error}",
            ) from error

        deadline = time.monotonic() + self._timeout_s
        try:
            stdout = _exchange_bounded(
                process,
                request_bytes,
                self._max_output_bytes,
                deadline,
            )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise PlannerError("planner_timeout", "planner timeout")
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired as error:
                raise PlannerError("planner_timeout", "planner timeout") from error
        except BaseException:
            _kill_process_group(process)
            _drain_and_wait(process)
            raise

        if process.returncode != 0:
            raise PlannerError(
                "planner_process_failed",
                f"planner process failed with exit code {process.returncode}",
            )
        return stdout


def _exchange_bounded(
    process: subprocess.Popen[bytes],
    request_bytes: bytes,
    max_output_bytes: int,
    deadline: float,
) -> bytes:
    assert process.stdin is not None
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    stdout = bytearray()
    input_offset = 0
    try:
        os.set_blocking(process.stdin.fileno(), False)
        os.set_blocking(process.stdout.fileno(), False)
        selector.register(process.stdin, selectors.EVENT_WRITE, "stdin")
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")

        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise PlannerError("planner_timeout", "planner timeout")
            events = selector.select(remaining)
            if not events:
                raise PlannerError("planner_timeout", "planner timeout")

            for key, _ in events:
                pipe = key.fileobj
                if key.data == "stdin":
                    try:
                        written = os.write(
                            pipe.fileno(),
                            request_bytes[input_offset : input_offset + 16 * 1024],
                        )
                    except BrokenPipeError:
                        written = len(request_bytes) - input_offset
                    input_offset += written
                    if input_offset == len(request_bytes):
                        selector.unregister(pipe)
                        pipe.close()
                    continue

                try:
                    chunk = os.read(pipe.fileno(), 16 * 1024)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(pipe)
                    pipe.close()
                    continue
                if len(stdout) + len(chunk) > max_output_bytes:
                    raise PlannerError(
                        "planner_output_too_large",
                        "planner output exceeds configured limit",
                    )
                stdout.extend(chunk)
    finally:
        selector.close()
    return bytes(stdout)


def _kill_process_group(process: subprocess.Popen[bytes]) -> None:
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    elif process.poll() is None:
        process.kill()


def _drain_and_wait(process: subprocess.Popen[bytes]) -> None:
    if process.stdin is not None and not process.stdin.closed:
        process.stdin.close()
    if process.stdout is not None and not process.stdout.closed:
        if os.name == "posix":
            os.set_blocking(process.stdout.fileno(), True)
            while process.stdout.read(64 * 1024):
                pass
        process.stdout.close()
    process.wait()


def _is_canonical_plan(plan: object) -> bool:
    return (
        isinstance(plan, dict)
        and plan.get("message_type") == "task_plan"
        and isinstance(plan.get("task_id"), str)
        and bool(plan["task_id"])
        and isinstance(plan.get("waypoints"), list)
        and bool(plan["waypoints"])
    )


def store_plan_atomic(plan: Mapping[str, Any], output_path: Path) -> None:
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=output_path.parent,
            prefix=f".{output_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            json.dump(
                plan,
                temporary_file,
                ensure_ascii=False,
                separators=(",", ":"),
            )
            temporary_file.flush()
        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
