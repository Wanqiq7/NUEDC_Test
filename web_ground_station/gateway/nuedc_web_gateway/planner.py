import asyncio
import json
import os
import tempfile
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

        try:
            process = await asyncio.create_subprocess_exec(
                str(self._executable), stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
            )
        except OSError as error:
            raise PlannerError(
                "planner_process_failed",
                f"planner process could not start: {error}",
            ) from error

        try:
            stdout, _ = await asyncio.wait_for(
                process.communicate(input=request_bytes),
                timeout=self._timeout_s,
            )
        except asyncio.TimeoutError as error:
            try:
                process.kill()
            except ProcessLookupError:
                pass
            await process.wait()
            raise PlannerError("planner_timeout", "planner timeout") from error

        if process.returncode != 0:
            raise PlannerError(
                "planner_process_failed",
                f"planner process failed with exit code {process.returncode}",
            )
        if len(stdout) > self._max_output_bytes:
            raise PlannerError(
                "planner_output_too_large",
                "planner output exceeds configured limit",
            )

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
