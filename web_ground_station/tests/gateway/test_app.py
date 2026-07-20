import asyncio
import json

import httpx
import pytest

import nuedc_web_gateway.app as app_module
from nuedc_web_gateway.airborne import GroundControlCommand
from nuedc_web_gateway.app import GatewayServices, create_app
from nuedc_web_gateway.config import GatewayConfig
from nuedc_web_gateway.models import AckSnapshot, WebEvent
from nuedc_web_gateway.planner import PlannerError
from nuedc_web_gateway.state import GroundState


def plan_for(task_id="case-1"):
    return {
        "message_type": "task_plan",
        "task_id": task_id,
        "task_type": "h_problem",
        "waypoints": [{"id": "A9B1"}],
    }


def ack_for(task_id="case-1", loaded=True, running=False, seq=1):
    return AckSnapshot(
        ok=True,
        message="accepted",
        task_id=task_id,
        mission_loaded=loaded,
        mission_running=running,
        last_accepted_sequence=seq,
        vision_armed=False,
    )


class FakePlanner:
    def __init__(self):
        self.result, self.error = plan_for(), None
        self.requests = []

    async def plan(self, request):
        self.requests.append(request)
        if self.error:
            raise self.error
        return self.result


class FakeRecorder:
    def __init__(self):
        self.started = self.stopped = False

    async def start(self):
        self.started = True

    async def stop(self):
        self.stopped = True

    def record(self, event):
        return True


class FakeAirborne:
    def __init__(self, state):
        self.state, self.probe_ack, self.next_ack = state, None, ack_for()
        self.probe_count, self.commands, self.loads = 0, [], []
        self.loop_users, self.closed = 0, False
        self.probe_gate = None
        self.heartbeat_started = False

    async def probe_once(self):
        self.probe_count += 1
        if self.probe_gate is not None:
            await self.probe_gate.wait()
        return self.probe_ack or ack_for("", loaded=False)

    async def heartbeat_loop(self, stop):
        self.heartbeat_started = True
        self.loop_users += 1
        try:
            while not stop.is_set():
                await asyncio.sleep(0)
        finally:
            self.loop_users -= 1

    async def telemetry_loop(self, stop):
        self.loop_users += 1
        try:
            while not stop.is_set():
                await asyncio.sleep(0)
        finally:
            self.loop_users -= 1

    async def send_mission_load(self, plan):
        self.loads.append(plan)
        return self.next_ack

    async def send_control(self, command, task_id):
        self.commands.append((command, task_id))
        return self.next_ack

    def close(self):
        assert self.loop_users == 0
        self.closed = True


def services(tmp_path):
    state = GroundState()
    airborne = FakeAirborne(state)
    planner = FakePlanner()
    recorder = FakeRecorder()
    config = GatewayConfig(
        "127.0.0.1",
        5557,
        5558,
        False,
        9870,
        "127.0.0.1",
        8000,
        tmp_path,
        tmp_path / "planner",
    )
    return (
        GatewayServices(config, planner, state, recorder, airborne),
        planner,
        recorder,
        airborne,
    )


async def request(app, method, path, **kwargs):
    async with httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="http://test"
    ) as client:
        return await client.request(method, path, **kwargs)


async def wait_command_online(state):
    for _ in range(20):
        if state.snapshot(1).command_link == "online":
            return
        await asyncio.sleep(0)
    raise AssertionError("command link did not become online")


def test_app_factory_uses_exported_runtime_environment(monkeypatch, tmp_path):
    planner_path = tmp_path / "planner"
    captured = {}

    class FactoryPlanner:
        def __init__(self, path, **kwargs):
            captured["planner_path"] = path

    class FactoryRecorder:
        def __init__(self, path, state):
            captured["recorder_path"] = path

    class FactoryAirborne:
        def __init__(self, config, state, recorder):
            captured["config"] = config

    monkeypatch.setenv("NUEDC_AIRBORNE_HOST", "192.0.2.20")
    monkeypatch.setenv("NUEDC_TELEMETRY_PORT", "16557")
    monkeypatch.setenv("NUEDC_COMMAND_PORT", "16558")
    monkeypatch.setenv("NUEDC_RUNTIME_DIR", str(tmp_path))
    monkeypatch.setenv("NUEDC_PLANNER_CLI", str(planner_path))
    monkeypatch.setattr(app_module, "PlannerClient", FactoryPlanner)
    monkeypatch.setattr(app_module, "JsonlRecorder", FactoryRecorder)
    monkeypatch.setattr(app_module, "AirborneClient", FactoryAirborne)

    application = app_module.create_app()

    assert application.state.services.config is captured["config"]
    assert captured["config"].telemetry_endpoint == "tcp://192.0.2.20:16557"
    assert captured["config"].command_endpoint == "tcp://192.0.2.20:16558"
    assert captured["planner_path"] == planner_path
    assert captured["recorder_path"] == tmp_path / "sessions"


@pytest.mark.asyncio
async def test_health_snapshot_probe_and_lifespan(tmp_path):
    svc, _, recorder, airborne = services(tmp_path)
    airborne.probe_ack = ack_for("case-1")
    svc.state.apply_plan(plan_for(), 1)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        response = await request(app, "GET", "/api/health")
        await asyncio.sleep(0)
        assert response.json() == {"ok": True}
        assert recorder.started
        assert airborne.probe_count == 1
    assert recorder.stopped and airborne.closed


@pytest.mark.asyncio
async def test_initial_probe_completes_before_heartbeat(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    airborne.probe_ack = ack_for("case-1", loaded=True)
    airborne.probe_gate = asyncio.Event()
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await asyncio.sleep(0)
        assert airborne.probe_count == 1
        assert not airborne.heartbeat_started
        assert svc.state.snapshot(1).command_link == "resyncing"
        airborne.probe_gate.set()
        for _ in range(10):
            await asyncio.sleep(0)
            if airborne.heartbeat_started:
                break
        assert airborne.heartbeat_started


@pytest.mark.asyncio
async def test_plan_success_persists(tmp_path):
    svc, planner, _, _ = services(tmp_path)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await wait_command_online(svc.state)
        response = await request(
            app,
            "POST",
            "/api/mission/plan",
            json={"case_path": "shared/cases/case.json", "no_fly_cells": []},
        )
    assert response.status_code == 200
    assert (
        json.loads((tmp_path / "active_mission_plan.json").read_text())
        == planner.result
    )
    assert planner.requests[0].case_path.endswith("/shared/cases/case.json")


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "code,status",
    [
        ("invalid_no_fly_zone", 409),
        ("planner_timeout", 504),
        ("planner_process_failed", 502),
    ],
)
async def test_plan_failure_keeps_file(tmp_path, code, status):
    path = tmp_path / "active_mission_plan.json"
    path.write_text(json.dumps(plan_for("old")))
    previous = path.read_text()
    svc, planner, _, _ = services(tmp_path)
    planner.error = PlannerError(code, "failed")
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await wait_command_online(svc.state)
        response = await request(
            app,
            "POST",
            "/api/mission/plan",
            json={"case_path": "shared/cases/case.json", "no_fly_cells": []},
        )
    assert response.status_code == status and path.read_text() == previous


@pytest.mark.asyncio
async def test_invalid_request_422(tmp_path):
    svc, *_ = services(tmp_path)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        response = await request(app, "POST", "/api/mission/plan", json={})
    assert response.status_code == 422


@pytest.mark.asyncio
async def test_load_and_start_use_ack(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for(), 1)
    airborne.probe_ack = ack_for("case-1", loaded=True)
    airborne.next_ack = ack_for("case-1", loaded=True, running=True, seq=2)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await wait_command_online(svc.state)
        airborne.next_ack = ack_for("case-1", loaded=True, running=False, seq=2)
        load = await request(app, "POST", "/api/mission/load")
        airborne.next_ack = ack_for("case-1", loaded=True, running=True, seq=3)
        start = await request(app, "POST", "/api/mission/start")
    assert (
        load.status_code == 200
        and start.json()["ack"]["mission_running"]
        and airborne.commands == [(GroundControlCommand.START, "case-1")]
    )


@pytest.mark.asyncio
async def test_start_rejected_until_matching_loaded_ack(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for(), 1)
    airborne.probe_ack = ack_for("other", loaded=True)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        response = await request(app, "POST", "/api/mission/start")
    assert (
        response.status_code == 409
        and response.json()["error_code"] == "mission_not_ready"
    )


@pytest.mark.asyncio
async def test_stop_rejected_when_not_running(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for(), 1)
    airborne.probe_ack = ack_for(loaded=True)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        response = await request(app, "POST", "/api/mission/stop")
    assert (
        response.status_code == 409
        and response.json()["error_code"] == "mission_not_running"
    )


@pytest.mark.asyncio
async def test_probe_applies_ack(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for(), 1)
    airborne.probe_ack = ack_for(loaded=True)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        response = await request(app, "POST", "/api/link/probe")
    assert response.status_code == 200 and response.json()["ack"]["mission_loaded"]


@pytest.mark.asyncio
@pytest.mark.parametrize("path", ["/api/mission/plan", "/api/mission/load"])
async def test_plan_and_load_rejected_while_airborne_mission_runs(tmp_path, path):
    svc, planner, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for("display-plan"), 1)
    airborne.probe_ack = ack_for("airborne-task", loaded=True, running=True)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await wait_command_online(svc.state)
        kwargs = (
            {"json": {"case_path": "shared/cases/case.json", "no_fly_cells": []}}
            if path.endswith("plan")
            else {}
        )
        response = await request(app, "POST", path, **kwargs)
    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_running"
    assert not planner.requests
    assert not airborne.loads


@pytest.mark.asyncio
async def test_mismatched_running_task_can_still_be_stopped(tmp_path):
    svc, _, _, airborne = services(tmp_path)
    svc.state.apply_plan(plan_for("display-plan"), 1)
    airborne.probe_ack = ack_for("airborne-task", loaded=True, running=True)
    airborne.next_ack = ack_for("airborne-task", loaded=True, running=False, seq=2)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await wait_command_online(svc.state)
        response = await request(app, "POST", "/api/mission/stop")
    assert response.status_code == 200
    assert airborne.commands == [(GroundControlCommand.STOP, "airborne-task")]


def test_restart_plan_resyncing(tmp_path):
    (tmp_path / "active_mission_plan.json").write_text(json.dumps(plan_for()))
    svc, *_ = services(tmp_path)
    assert (
        svc.state.snapshot(1).command_link == "resyncing"
        and not svc.state.snapshot(1).mission_loaded
    )


class FakeWebSocket:
    def __init__(self):
        self.sent, self.accepted = [], False
        self.received = asyncio.Queue()

    async def accept(self):
        self.accepted = True

    async def send_json(self, value):
        self.sent.append(value)

    async def receive(self):
        return await self.received.get()


@pytest.mark.asyncio
async def test_websocket_snapshot_and_incremental_unsubscribe(tmp_path):
    svc, *_ = services(tmp_path)
    app = create_app(svc)
    ws = FakeWebSocket()
    endpoint = next(
        r.endpoint for r in app.routes if getattr(r, "path", "") == "/ws/telemetry"
    )
    task = asyncio.create_task(endpoint(ws))
    await asyncio.sleep(0)
    assert ws.sent[0]["type"] == "snapshot"
    svc.state.publish_event(
        WebEvent(type="task_event", seq=3, timestamp_ms=1, payload={})
    )
    await asyncio.sleep(0.01)
    assert ws.sent[1]["seq"] == 3
    ws.received.put_nowait({"type": "websocket.disconnect"})
    await task
    assert not svc.state._subscribers


@pytest.mark.asyncio
async def test_lifespan_shutdown_active_loops(tmp_path):
    svc, _, recorder, airborne = services(tmp_path)
    app = create_app(svc)
    async with app.router.lifespan_context(app):
        await asyncio.sleep(0.01)
        assert airborne.loop_users == 2
    assert recorder.stopped and airborne.closed
