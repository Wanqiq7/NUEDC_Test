import asyncio
import json
from pathlib import Path

from fastapi.testclient import TestClient
import pytest

from nuedc_web_gateway.airborne import GroundControlCommand
from nuedc_web_gateway.app import GatewayServices, create_app
from nuedc_web_gateway.config import GatewayConfig
from nuedc_web_gateway.models import AckSnapshot, WebEvent
from nuedc_web_gateway.planner import PlannerError
from nuedc_web_gateway.state import GroundState


def plan_for(task_id: str = "case-1") -> dict:
    return {
        "message_type": "task_plan",
        "task_id": task_id,
        "task_type": "h_problem",
        "waypoints": [{"id": "A9B1"}],
    }


def ack_for(
    task_id: str = "case-1", *, loaded: bool = True, running: bool = False, seq: int = 1
) -> AckSnapshot:
    return AckSnapshot(
        ok=True,
        message="accepted",
        task_id=task_id,
        mission_loaded=loaded,
        mission_running=running,
        last_accepted_sequence=seq,
        vision_armed=False,
    )


def gateway_config(runtime_dir: Path) -> GatewayConfig:
    return GatewayConfig(
        airborne_host="127.0.0.1",
        telemetry_port=5557,
        command_port=5558,
        pid_debug_enabled=False,
        pid_debug_port=9870,
        web_host="127.0.0.1",
        web_port=8000,
        runtime_dir=runtime_dir,
        planner_cli=runtime_dir / "planner",
    )


class FakePlanner:
    def __init__(self) -> None:
        self.result = plan_for()
        self.error: PlannerError | None = None
        self.requests = []

    async def plan(self, request):
        self.requests.append(request)
        if self.error is not None:
            raise self.error
        return self.result


class FakeRecorder:
    def __init__(self) -> None:
        self.started = False
        self.stopped = False

    async def start(self) -> None:
        self.started = True

    async def stop(self) -> None:
        self.stopped = True


class FakeAirborne:
    def __init__(self) -> None:
        self.probe_ack: AckSnapshot | None = None
        self.next_ack = ack_for()
        self.probe_count = 0
        self.commands = []
        self.loads = []
        self.loop_users = 0
        self.loops_exited = asyncio.Event()
        self.closed = False

    async def probe_once(self) -> AckSnapshot:
        self.probe_count += 1
        if self.probe_ack is None:
            return ack_for("", loaded=False)
        return self.probe_ack

    async def heartbeat_loop(self, stop: asyncio.Event) -> None:
        self.loop_users += 1
        try:
            ack = await self.probe_once()
            if ack.ok:
                self._state.apply_ack(ack, 1_000)
            while not stop.is_set():
                await asyncio.sleep(0.005)
        finally:
            self.loop_users -= 1
            if self.loop_users == 0:
                self.loops_exited.set()

    async def telemetry_loop(self, stop: asyncio.Event) -> None:
        self.loop_users += 1
        try:
            while not stop.is_set():
                await asyncio.sleep(0.005)
        finally:
            self.loop_users -= 1
            if self.loop_users == 0:
                self.loops_exited.set()

    async def send_mission_load(self, plan: dict) -> AckSnapshot:
        self.loads.append(plan)
        return self.next_ack

    async def send_control(
        self, command: GroundControlCommand, task_id: str
    ) -> AckSnapshot:
        self.commands.append((command, task_id))
        return self.next_ack

    def close(self) -> None:
        assert self.loop_users == 0
        self.closed = True


def build_services(
    runtime_dir: Path,
    *,
    state: GroundState | None = None,
    airborne: FakeAirborne | None = None,
) -> tuple[GatewayServices, FakePlanner, FakeRecorder, FakeAirborne]:
    current_state = state or GroundState()
    fake_airborne = airborne or FakeAirborne()
    fake_airborne._state = current_state
    planner = FakePlanner()
    recorder = FakeRecorder()
    services = GatewayServices(
        config=gateway_config(runtime_dir),
        planner=planner,
        state=current_state,
        recorder=recorder,
        airborne=fake_airborne,
    )
    return services, planner, recorder, fake_airborne


def test_health_snapshot_and_lifespan_probe(tmp_path):
    services, _, recorder, airborne = build_services(tmp_path)
    airborne.probe_ack = ack_for("case-1")
    services.state.apply_plan(plan_for(), 100)

    with TestClient(create_app(services)) as client:
        assert client.get("/api/health").json() == {"ok": True}
        snapshot = client.get("/api/snapshot").json()
        assert snapshot["active_task_id"] == "case-1"
        assert snapshot["command_link"] == "online"
        assert airborne.probe_count == 1
        assert recorder.started is True

    assert recorder.stopped is True
    assert airborne.closed is True


def test_plan_success_persists_then_updates_snapshot(tmp_path):
    services, planner, _, _ = build_services(tmp_path)

    with TestClient(create_app(services)) as client:
        response = client.post(
            "/api/mission/plan",
            json={"case_path": "shared/cases/sample_case.json", "no_fly_cells": []},
        )

    assert response.status_code == 200
    assert response.json()["plan"] == planner.result
    assert json.loads((tmp_path / "active_mission_plan.json").read_text()) == planner.result
    assert services.state.snapshot(200).active_task_id == "case-1"


@pytest.mark.parametrize(
    ("error_code", "status"),
    [
        ("invalid_no_fly_zone", 409),
        ("planner_timeout", 504),
        ("planner_process_failed", 502),
        ("planner_invalid_response", 502),
    ],
)
def test_plan_failure_keeps_previous_plan(tmp_path, error_code, status):
    runtime_path = tmp_path / "active_mission_plan.json"
    runtime_path.write_text(json.dumps(plan_for("previous")))
    services, planner, _, _ = build_services(tmp_path)
    previous = runtime_path.read_text()
    planner.error = PlannerError(error_code, "planning failed")

    with TestClient(create_app(services)) as client:
        response = client.post(
            "/api/mission/plan", json={"case_path": "case.json", "no_fly_cells": []}
        )

    assert response.status_code == status
    assert response.json()["error_code"] == error_code
    assert runtime_path.read_text() == previous


def test_invalid_plan_request_returns_422(tmp_path):
    services, _, _, _ = build_services(tmp_path)
    with TestClient(create_app(services)) as client:
        response = client.post("/api/mission/plan", json={"no_fly_cells": "A1B1"})
    assert response.status_code == 422


def test_load_returns_ack_and_applies_only_ack_state(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    services.state.apply_plan(plan_for(), 100)
    airborne.probe_ack = ack_for("case-1", loaded=False)
    airborne.next_ack = ack_for("case-1", loaded=True, seq=2)

    with TestClient(create_app(services)) as client:
        response = client.post("/api/mission/load")

    assert response.status_code == 200
    assert response.json()["ack"]["mission_loaded"] is True
    assert services.state.snapshot(1_001).mission_loaded is True


def test_start_requires_online_matching_loaded_ack(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    services.state.apply_plan(plan_for(), 100)
    airborne.probe_ack = ack_for("old", loaded=True)

    with TestClient(create_app(services)) as client:
        response = client.post("/api/mission/start")

    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_not_ready"
    assert airborne.commands == []


def test_start_returns_ack_without_optimistic_running_state(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    services.state.apply_plan(plan_for(), 100)
    airborne.probe_ack = ack_for("case-1", loaded=True, seq=1)
    airborne.next_ack = ack_for("case-1", loaded=True, running=True, seq=2)

    with TestClient(create_app(services)) as client:
        response = client.post("/api/mission/start")

    assert response.status_code == 200
    assert response.json()["ack"]["mission_running"] is True
    assert airborne.commands == [(GroundControlCommand.START, "case-1")]
    assert services.state.snapshot(1_001).mission_running is True


def test_stop_requires_online_running_mission(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    services.state.apply_plan(plan_for(), 100)
    airborne.probe_ack = ack_for("case-1", loaded=True, running=False)

    with TestClient(create_app(services)) as client:
        response = client.post("/api/mission/stop")

    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_not_running"
    assert airborne.commands == []


def test_probe_returns_and_applies_ack(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    services.state.apply_plan(plan_for(), 100)
    airborne.probe_ack = ack_for("case-1", loaded=True)

    with TestClient(create_app(services)) as client:
        response = client.post("/api/link/probe")

    assert response.status_code == 200
    assert response.json()["ack"]["mission_loaded"] is True
    assert airborne.probe_count == 2


def test_restart_restores_plan_for_display_in_resyncing_state(tmp_path):
    (tmp_path / "active_mission_plan.json").write_text(json.dumps(plan_for()))

    services, _, _, _ = build_services(tmp_path)
    snapshot = services.state.snapshot(100)

    assert snapshot.active_task_id == "case-1"
    assert snapshot.mission_loaded is False
    assert snapshot.mission_running is False
    assert snapshot.command_link == "resyncing"


def test_websocket_sends_snapshot_then_event_and_unsubscribes(tmp_path):
    services, _, _, airborne = build_services(tmp_path)
    airborne.probe_ack = ack_for("", loaded=False)
    app = create_app(services)

    with TestClient(app) as client:
        with client.websocket_connect("/ws/telemetry") as socket:
            snapshot = socket.receive_json()
            assert snapshot["type"] == "snapshot"
            services.state.publish_event(
                WebEvent(type="task_event", seq=3, timestamp_ms=100, payload={})
            )
            assert socket.receive_json()["seq"] == 3
        assert len(services.state._subscribers) == 0


def test_shutdown_waits_for_active_loops_before_closing_airborne(tmp_path):
    services, _, _, airborne = build_services(tmp_path)

    with TestClient(create_app(services)):
        assert airborne.loop_users == 2

    assert airborne.loops_exited.is_set()
    assert airborne.closed is True
