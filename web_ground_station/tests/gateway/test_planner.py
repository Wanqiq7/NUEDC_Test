import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

import pytest

from nuedc_web_gateway.models import PlanningRequest
from nuedc_web_gateway.planner import PlannerClient, PlannerError, store_plan_atomic


def make_executable(tmp_path: Path, script: str) -> Path:
    executable = tmp_path / "planner"
    executable.write_text(script)
    executable.chmod(executable.stat().st_mode | 0o111)
    return executable


def make_python_executable(tmp_path: Path, body: str) -> Path:
    return make_executable(tmp_path, f"#!{sys.executable}\n{body}")


@pytest.mark.asyncio
async def test_returns_canonical_plan_from_versioned_request(tmp_path):
    request_capture = tmp_path / "request.json"
    executable = make_python_executable(
        tmp_path,
        "import pathlib, sys\n"
        f"pathlib.Path({str(request_capture)!r}).write_bytes(sys.stdin.buffer.read())\n"
        "sys.stdout.write('{\"ok\":true,\"plan\":{\"message_type\":\"task_plan\","
        "\"task_id\":\"case-1\",\"waypoints\":[{\"id\":\"A9B1\"}]},"
        "\"metrics\":{}}')\n",
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


@pytest.mark.asyncio
async def test_runs_sync_popen_in_worker_thread_without_shell(tmp_path, monkeypatch):
    executable = make_python_executable(
        tmp_path,
        "import sys\n"
        "sys.stdin.buffer.read()\n"
        "sys.stdout.write('{\"ok\":true,\"plan\":{\"message_type\":\"task_plan\","
        "\"task_id\":\"case-1\",\"waypoints\":[{}]}}')\n",
    )
    main_thread_id = threading.get_ident()
    popen_calls = []
    real_popen = subprocess.Popen

    def capture_popen(*args, **kwargs):
        popen_calls.append((threading.get_ident(), args, kwargs))
        return real_popen(*args, **kwargs)

    monkeypatch.setattr(subprocess, "Popen", capture_popen)
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)
    loop_progress = asyncio.create_task(asyncio.sleep(0))

    await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert loop_progress.done()
    assert len(popen_calls) == 1
    thread_id, args, kwargs = popen_calls[0]
    assert thread_id != main_thread_id
    assert args[0] == [str(executable)]
    assert kwargs["stdin"] is subprocess.PIPE
    assert kwargs["stdout"] is subprocess.PIPE
    assert kwargs["stderr"] is subprocess.PIPE
    assert kwargs.get("shell", False) is False


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
    executable = make_python_executable(
        tmp_path,
        "import pathlib\n"
        f"pathlib.Path({str(spawn_marker)!r}).touch()\n",
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
    executable = make_python_executable(tmp_path, "import time\ntime.sleep(2)\n")
    client = PlannerClient(executable, timeout_s=0.01, max_output_bytes=4096)

    with pytest.raises(PlannerError, match="timeout") as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert caught.value.error_code == "planner_timeout"


@pytest.mark.asyncio
@pytest.mark.skipif(os.name != "posix", reason="POSIX process-group cleanup")
async def test_timeout_kills_planner_process_group(tmp_path):
    child_marker = tmp_path / "child-finished"
    child_code = (
        "import pathlib, time; "
        "time.sleep(0.2); "
        f"pathlib.Path({str(child_marker)!r}).touch()"
    )
    executable = make_python_executable(
        tmp_path,
        "import subprocess, sys, time\n"
        f"subprocess.Popen([sys.executable, '-c', {child_code!r}])\n"
        "time.sleep(2)\n",
    )
    client = PlannerClient(executable, timeout_s=0.05, max_output_bytes=4096)

    with pytest.raises(PlannerError) as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))
    await asyncio.sleep(0.3)

    assert caught.value.error_code == "planner_timeout"
    assert not child_marker.exists()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("script", "error_code"),
    [
        ("import sys\nsys.exit(4)\n", "planner_process_failed"),
        ("import sys\nsys.stdout.write('not-json')\n", "planner_invalid_response"),
        ("import sys\nsys.stdout.write('0' * 5000)\n", "planner_output_too_large"),
    ],
)
async def test_rejects_failed_or_invalid_planner_output(
    tmp_path, script, error_code
):
    executable = make_python_executable(tmp_path, script)
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
    executable = make_python_executable(
        tmp_path,
        "import sys\n"
        f"sys.stdout.write({encoded_response!r})\n",
    )
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)

    with pytest.raises(PlannerError) as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

    assert caught.value.error_code == "planner_invalid_response"
