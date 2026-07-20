import json
import os
from pathlib import Path

import pytest

from nuedc_web_gateway.models import PlanningRequest
from nuedc_web_gateway.planner import PlannerClient, PlannerError, store_plan_atomic


def make_executable(tmp_path: Path, script: str) -> Path:
    executable = tmp_path / "planner"
    executable.write_text(script)
    executable.chmod(executable.stat().st_mode | 0o111)
    return executable


@pytest.mark.asyncio
async def test_returns_canonical_plan_from_versioned_request(tmp_path):
    request_capture = tmp_path / "request.json"
    executable = make_executable(
        tmp_path,
        "#!/bin/sh\n"
        f"tee '{request_capture}' >/dev/null\n"
        "printf '%s' '{\"ok\":true,\"plan\":{\"message_type\":\"task_plan\","
        "\"task_id\":\"case-1\",\"waypoints\":[{\"id\":\"A9B1\"}]},"
        "\"metrics\":{}}'\n",
    )
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)

    plan = await client.plan(
        PlanningRequest(
            case_path="shared/cases/sample_case.json",
            no_fly_cells=["A1B2"],
        )
    )

    assert plan["task_id"] == "case-1"
    assert json.loads(request_capture.read_text()) == {
        "schema": "h_planning_request_v1",
        "case_path": "shared/cases/sample_case.json",
        "no_fly_cells": ["A1B2"],
    }


def test_stores_plan_by_replacing_from_sibling_temp_file(tmp_path, monkeypatch):
    output = tmp_path / "active.json"
    output.write_text('{"task_id":"old"}')
    replacement = {}
    real_replace = os.replace

    def capture_replace(source, destination):
        source_path = Path(source)
        replacement["source_parent"] = source_path.parent
        replacement["content"] = source_path.read_text()
        real_replace(source, destination)

    monkeypatch.setattr(os, "replace", capture_replace)

    store_plan_atomic(
        {
            "message_type": "task_plan",
            "task_id": "case-1",
            "waypoints": [{"id": "A9B1"}],
        },
        output,
    )

    assert replacement["source_parent"] == output.parent
    assert json.loads(replacement["content"])["task_id"] == "case-1"
    assert json.loads(output.read_text())["task_id"] == "case-1"


@pytest.mark.asyncio
async def test_rejects_stdin_above_64_kib_before_spawning(tmp_path):
    spawn_marker = tmp_path / "spawned"
    executable = make_executable(
        tmp_path,
        f"#!/bin/sh\ntouch '{spawn_marker}'\n",
    )
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)

    with pytest.raises(PlannerError) as caught:
        await client.plan(
            PlanningRequest(case_path="x" * (64 * 1024), no_fly_cells=[])
        )

    assert caught.value.error_code == "planner_input_too_large"
    assert not spawn_marker.exists()


@pytest.mark.asyncio
async def test_times_out(tmp_path):
    executable = make_executable(tmp_path, "#!/bin/sh\nsleep 2\n")
    client = PlannerClient(executable, timeout_s=0.01, max_output_bytes=4096)

    with pytest.raises(PlannerError, match="timeout") as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert caught.value.error_code == "planner_timeout"


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("script", "error_code"),
    [
        ("#!/bin/sh\ncat >/dev/null\nexit 4\n", "planner_process_failed"),
        ("#!/bin/sh\ncat >/dev/null\nprintf 'not-json'\n", "planner_invalid_response"),
        ("#!/bin/sh\ncat >/dev/null\nprintf '%05000d' 0\n", "planner_output_too_large"),
    ],
)
async def test_rejects_failed_or_invalid_planner_output(
    tmp_path, script, error_code
):
    executable = make_executable(tmp_path, script)
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)

    with pytest.raises(PlannerError) as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert caught.value.error_code == error_code


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "response",
    [
        [],
        {"ok": False, "error": "no route"},
        {"ok": True, "plan": []},
        {
            "ok": True,
            "plan": {"message_type": "other", "task_id": "case-1", "waypoints": [{}]},
        },
        {
            "ok": True,
            "plan": {"message_type": "task_plan", "task_id": "", "waypoints": [{}]},
        },
        {
            "ok": True,
            "plan": {"message_type": "task_plan", "task_id": "case-1", "waypoints": []},
        },
    ],
)
async def test_rejects_noncanonical_planner_response(tmp_path, response):
    encoded_response = json.dumps(response, separators=(",", ":"))
    executable = make_executable(
        tmp_path,
        f"#!/bin/sh\ncat >/dev/null\nprintf '%s' '{encoded_response}'\n",
    )
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)

    with pytest.raises(PlannerError) as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert caught.value.error_code == "planner_invalid_response"
