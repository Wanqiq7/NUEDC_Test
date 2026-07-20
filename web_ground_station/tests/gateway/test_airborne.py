import asyncio
from collections import deque
import json
from pathlib import Path
import socket

import pytest
import zmq
import zmq.asyncio

from nuedc_web_gateway.airborne import AirborneClient, GroundControlCommand
from nuedc_web_gateway.config import GatewayConfig
from nuedc_web_gateway.models import AckSnapshot
from nuedc_web_gateway.proto_runtime import load_messages_module
from nuedc_web_gateway.state import GroundState


MESSAGES = load_messages_module()


def now_ms() -> int:
    return 1_000


def reserve_port() -> int:
    with socket.socket() as candidate:
        candidate.bind(("127.0.0.1", 0))
        return candidate.getsockname()[1]


def config(tmp_path: Path, command_port: int, telemetry_port: int) -> GatewayConfig:
    return GatewayConfig(
        airborne_host="127.0.0.1",
        telemetry_port=telemetry_port,
        command_port=command_port,
        pid_debug_enabled=False,
        pid_debug_port=9870,
        web_host="127.0.0.1",
        web_port=8000,
        runtime_dir=tmp_path,
        planner_cli=tmp_path / "planner",
    )


def plan_for(task_id: str) -> dict:
    return {
        "message_type": "task_plan",
        "task_id": task_id,
        "task_type": "h_problem",
        "start_waypoint_id": "A9B1",
        "terminal_waypoint_id": "A8B1",
        "metadata_json": '{ "keep": "spacing" }',
        "waypoints": [
            {
                "id": "A9B1",
                "sequence_index": 4,
                "x": 1.0,
                "y": 2.0,
                "z": 3.0,
                "action": "inspect",
                "payload_json": '{ "raw": true }',
            }
        ],
    }


def running_ack(task_id: str) -> AckSnapshot:
    return AckSnapshot(
        ok=True,
        message="running",
        task_id=task_id,
        mission_loaded=True,
        mission_running=True,
        last_accepted_sequence=1,
        vision_armed=False,
    )


def task_event(task_id: str, event: str, envelope_sequence: int, payload: dict):
    envelope = MESSAGES.Envelope(sequence=envelope_sequence, timestamp_ms=900)
    envelope.task_event.task_id = task_id
    envelope.task_event.event_type = event
    envelope.task_event.sequence_index = 7
    envelope.task_event.waypoint_id = "A8B1"
    envelope.task_event.payload_json = json.dumps(payload)
    return envelope


def task_summary(task_id: str, success: bool):
    envelope = MESSAGES.Envelope(sequence=12, timestamp_ms=901)
    envelope.task_summary.task_id = task_id
    envelope.task_summary.task_type = "h_problem"
    envelope.task_summary.success = success
    envelope.task_summary.visited_waypoints = 3
    envelope.task_summary.payload_json = '{"detections":2}'
    return envelope


class RecordingRecorder:
    def __init__(self, state: GroundState) -> None:
        self.state = state
        self.events = []
        self.cells_seen_at_record = []
        self.subscriber = None
        self.subscriber_empty_at_record = []
        self.result = True

    def record(self, event):
        self.events.append(event)
        self.cells_seen_at_record.append(self.state.snapshot(now_ms()).current_cell)
        if self.subscriber is not None:
            self.subscriber_empty_at_record.append(self.subscriber.empty())
        return self.result


class RepServer:
    def __init__(self, context, endpoint: str) -> None:
        self.socket = context.socket(zmq.REP)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.socket.bind(endpoint)
        self.replies = deque()
        self.requests = []
        self.request_count_changed = asyncio.Event()
        self.replies_allowed = asyncio.Event()
        self.replies_allowed.set()
        self.active_requests = 0
        self.max_simultaneous_requests = 0
        self.task = asyncio.create_task(self._serve())

    def queue_ack(self, **values) -> None:
        self.replies.append(values)

    def block_replies(self) -> None:
        self.replies_allowed.clear()

    def release_replies(self) -> None:
        self.replies_allowed.set()

    async def wait_for_request_count(self, count: int) -> None:
        while len(self.requests) < count:
            self.request_count_changed.clear()
            await asyncio.wait_for(self.request_count_changed.wait(), timeout=2)

    async def _serve(self) -> None:
        try:
            while True:
                raw = await self.socket.recv()
                envelope = MESSAGES.Envelope.FromString(raw)
                self.requests.append(envelope)
                self.active_requests += 1
                self.max_simultaneous_requests = max(
                    self.max_simultaneous_requests, self.active_requests
                )
                self.request_count_changed.set()
                await self.replies_allowed.wait()
                values = self.replies.popleft() if self.replies else {}
                reply = MESSAGES.Envelope()
                reply.ack.success = values.get("success", True)
                reply.ack.message = values.get("message", "accepted")
                reply.ack.task_id = values.get(
                    "task_id", envelope.control_command.task_id
                )
                reply.ack.mission_loaded = values.get("mission_loaded", True)
                reply.ack.mission_running = values.get("mission_running", False)
                reply.ack.last_accepted_sequence = values.get(
                    "last_accepted_sequence", envelope.sequence
                )
                reply.ack.vision_armed = values.get("vision_armed", False)
                await self.socket.send(reply.SerializeToString())
                self.active_requests -= 1
        except asyncio.CancelledError:
            pass

    async def close(self) -> None:
        self.task.cancel()
        await self.task
        self.socket.close()


class PubServer:
    def __init__(self, context, endpoint: str) -> None:
        self.socket = context.socket(zmq.PUB)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.socket.bind(endpoint)

    async def publish(self, envelope) -> None:
        await asyncio.sleep(0.05)
        await self.socket.send(envelope.SerializeToString())

    async def publish_raw(self, payload: bytes) -> None:
        await asyncio.sleep(0.05)
        await self.socket.send(payload)

    def close(self) -> None:
        self.socket.close()


class RouterServer:
    def __init__(self, context, endpoint: str) -> None:
        self.socket = context.socket(zmq.ROUTER)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.socket.bind(endpoint)
        self.identities = []

    async def timeout_once_then_ack(self) -> None:
        first = await self.socket.recv_multipart()
        self.identities.append(first[0])
        second = await self.socket.recv_multipart()
        self.identities.append(second[0])
        request = MESSAGES.Envelope.FromString(second[-1])
        reply = MESSAGES.Envelope()
        reply.ack.success = True
        reply.ack.message = "accepted on retry"
        reply.ack.task_id = request.control_command.task_id
        reply.ack.last_accepted_sequence = request.sequence
        await self.socket.send_multipart([second[0], b"", reply.SerializeToString()])

    def close(self) -> None:
        self.socket.close()


@pytest.fixture
async def transport_fixture(tmp_path):
    context = zmq.asyncio.Context()
    command_port = reserve_port()
    telemetry_port = reserve_port()
    rep = RepServer(context, f"tcp://127.0.0.1:{command_port}")
    pub = PubServer(context, f"tcp://127.0.0.1:{telemetry_port}")
    state = GroundState()
    recorder = RecordingRecorder(state)
    client = AirborneClient(
        config(tmp_path, command_port, telemetry_port),
        state,
        recorder,
        context=context,
        timeout_ms=500,
        retry_delay_ms=5,
    )
    await asyncio.sleep(0.05)
    yield client, state, recorder, rep, pub
    client.close()
    await rep.close()
    pub.close()
    context.term()


@pytest.mark.asyncio
async def test_stale_ack_confirming_sequence_is_success(transport_fixture):
    client, _, _, rep_server, _ = transport_fixture
    rep_server.queue_ack(
        success=False,
        message="stale command",
        last_accepted_sequence=2**62,
    )

    ack = await client.send_control(GroundControlCommand.PING, "case-1")

    assert ack.ok is True
    assert ack.message == "command already accepted"


@pytest.mark.asyncio
async def test_stale_ping_ack_can_be_taskless(transport_fixture):
    client, _, _, rep_server, _ = transport_fixture
    for _ in range(3):
        rep_server.queue_ack(
            success=False,
            message="stale command",
            task_id="",
            last_accepted_sequence=2**62,
        )

    ack = await client.send_control(GroundControlCommand.PING, "case-1")

    assert ack.ok is True
    assert ack.message == "command already accepted"


@pytest.mark.asyncio
@pytest.mark.parametrize("ack_task_id", ["", "other-case"])
async def test_stateful_stale_ack_requires_exact_nonempty_task_id(
    transport_fixture, ack_task_id
):
    client, _, _, rep_server, _ = transport_fixture
    for _ in range(3):
        rep_server.queue_ack(
            success=False,
            message="stale command",
            task_id=ack_task_id,
            mission_running=True,
            last_accepted_sequence=2**62,
        )

    ack = await client.send_control(GroundControlCommand.START, "case-1")

    assert ack.ok is False
    assert ack.message == "stale command"


@pytest.mark.asyncio
async def test_stateful_stale_ack_accepts_matching_task_and_state(transport_fixture):
    client, _, _, rep_server, _ = transport_fixture
    rep_server.queue_ack(
        success=False,
        message="stale command",
        task_id="case-1",
        mission_running=True,
        last_accepted_sequence=2**62,
    )

    ack = await client.send_control(GroundControlCommand.START, "case-1")

    assert ack.ok is True
    assert ack.message == "command already accepted"


@pytest.mark.asyncio
async def test_commands_are_physically_serialized(transport_fixture):
    client, _, _, rep_server, _ = transport_fixture
    rep_server.block_replies()
    first = asyncio.create_task(
        client.send_control(GroundControlCommand.PING, "case-1")
    )
    second = asyncio.create_task(
        client.send_control(GroundControlCommand.PING, "case-1")
    )
    await rep_server.wait_for_request_count(1)
    await asyncio.sleep(0.02)

    assert len(rep_server.requests) == 1
    assert rep_server.max_simultaneous_requests == 1
    rep_server.release_replies()
    await asyncio.gather(first, second)
    assert len(rep_server.requests) == 2


@pytest.mark.asyncio
async def test_retry_uses_fresh_req_socket_after_first_timeout(tmp_path):
    context = zmq.asyncio.Context()
    command_port = reserve_port()
    telemetry_port = reserve_port()
    router = RouterServer(context, f"tcp://127.0.0.1:{command_port}")
    pub = PubServer(context, f"tcp://127.0.0.1:{telemetry_port}")
    state = GroundState()
    recorder = RecordingRecorder(state)
    client = AirborneClient(
        config(tmp_path, command_port, telemetry_port),
        state,
        recorder,
        context=context,
        timeout_ms=100,
        retry_delay_ms=5,
    )
    server = asyncio.create_task(router.timeout_once_then_ack())
    try:
        ack = await client.send_control(GroundControlCommand.PING, "case-1")
        await server

        assert ack.ok is True
        assert ack.message == "accepted on retry"
        assert len(router.identities) == 2
        assert router.identities[0] != router.identities[1]
    finally:
        server.cancel()
        await asyncio.gather(server, return_exceptions=True)
        client.close()
        router.close()
        pub.close()
        context.term()


class AlwaysTimeoutTransport:
    def __init__(self) -> None:
        self.attempts = 0

    async def send(self, payload: bytes) -> bytes:
        self.attempts += 1
        raise TimeoutError("command ack timed out")


class NonemptyTaskAckTransport:
    def __init__(self) -> None:
        self.attempts = 0

    async def send(self, payload: bytes) -> bytes:
        self.attempts += 1
        request = MESSAGES.Envelope.FromString(payload)
        reply = MESSAGES.Envelope()
        reply.ack.success = False
        reply.ack.message = "stale command"
        reply.ack.task_id = "case-1"
        reply.ack.mission_loaded = True
        reply.ack.mission_running = True
        reply.ack.last_accepted_sequence = request.sequence
        reply.ack.vision_armed = True
        return reply.SerializeToString()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "command",
    [
        GroundControlCommand.START,
        GroundControlCommand.STOP,
        GroundControlCommand.ARM_TARGETING,
        GroundControlCommand.DISARM_TARGETING,
    ],
)
async def test_stateful_command_rejects_empty_task_before_transport(
    transport_fixture, command
):
    client, _, _, _, _ = transport_fixture
    transport = NonemptyTaskAckTransport()
    client.replace_transport(transport)

    with pytest.raises(ValueError, match="task_id is required"):
        await client.send_control(command, "")

    assert transport.attempts == 0


@pytest.mark.asyncio
async def test_three_ping_failures_mark_command_offline(transport_fixture):
    client, ground_state, _, _, _ = transport_fixture
    transport = AlwaysTimeoutTransport()
    client.replace_transport(transport)

    for _ in range(3):
        await client.probe_once()

    assert transport.attempts == 9
    assert ground_state.snapshot(now_ms()).command_link == "offline"


@pytest.mark.asyncio
async def test_successful_ping_refreshes_link_when_airborne_ack_sequence_stays_zero(
    transport_fixture, monkeypatch
):
    client, ground_state, _, rep_server, _ = transport_fixture
    rep_server.queue_ack(last_accepted_sequence=0)
    rep_server.queue_ack(last_accepted_sequence=0)
    clock = iter([100, 100, 5_000, 5_000])
    monkeypatch.setattr("nuedc_web_gateway.airborne._now_ms", lambda: next(clock))

    first = await client.send_control(GroundControlCommand.PING, "case-1")
    second = await client.send_control(GroundControlCommand.PING, "case-1")

    assert first.last_accepted_sequence == 0
    assert second.last_accepted_sequence == 0
    assert ground_state.snapshot(5_001).command_link == "online"


@pytest.mark.asyncio
async def test_telemetry_never_marks_command_online(transport_fixture):
    client, ground_state, _, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    publish = asyncio.create_task(
        pub_server.publish(
            task_event("case-1", "telemetry", 1, {"current_cell": "A8B1"})
        )
    )
    await client.receive_one_telemetry()
    await publish

    assert ground_state.snapshot(now_ms()).command_link != "online"


@pytest.mark.asyncio
async def test_other_task_event_is_ignored(transport_fixture):
    client, ground_state, recorder, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    publish = asyncio.create_task(
        pub_server.publish(task_event("old", "telemetry", 1, {"current_cell": "A1B1"}))
    )
    await client.receive_one_telemetry()
    await publish

    assert ground_state.snapshot(now_ms()).current_cell is None
    assert recorder.events == []


@pytest.mark.asyncio
async def test_summary_updates_running_false(transport_fixture):
    client, ground_state, recorder, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    ground_state.apply_ack(running_ack("case-1"), now_ms())
    publish = asyncio.create_task(pub_server.publish(task_summary("case-1", True)))
    await client.receive_one_telemetry()
    await publish

    assert ground_state.snapshot(now_ms()).mission_running is False
    assert recorder.events[-1].payload == {
        "success": True,
        "task_type": "h_problem",
        "visited_waypoints": 3,
        "detections": 2,
    }


@pytest.mark.asyncio
async def test_event_uses_envelope_sequence_and_preserves_progress_in_payload(
    transport_fixture,
):
    client, ground_state, recorder, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    subscriber = ground_state.subscribe()
    recorder.subscriber = subscriber
    publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "case-1",
                "telemetry",
                44,
                {"current_cell": "A8B1", "visited_cells": 3},
            )
        )
    )
    await client.receive_one_telemetry()
    await publish

    event = recorder.events[-1]
    assert event.seq == 44
    assert event.payload == {
        "current_cell": "A8B1",
        "visited_cells": 3,
        "visited_count": 3,
        "sequence_index": 7,
        "waypoint_id": "A8B1",
    }
    assert recorder.cells_seen_at_record[-1] == "A8B1"
    assert recorder.subscriber_empty_at_record == [True]
    published = subscriber.get_nowait()
    assert published is event


@pytest.mark.asyncio
async def test_task_plan_records_then_publishes_exact_envelope_sequence(
    transport_fixture,
):
    client, ground_state, recorder, _, pub_server = transport_fixture
    subscriber = ground_state.subscribe()
    recorder.subscriber = subscriber
    envelope = MESSAGES.Envelope(sequence=73, timestamp_ms=902)
    plan = envelope.task_plan
    plan.task_id = "case-73"
    plan.task_type = "h_problem"
    plan.start_waypoint_id = "A9B1"
    plan.terminal_waypoint_id = "A8B1"
    plan.metadata_json = '{"source":"airborne"}'
    waypoint = plan.waypoints.add()
    waypoint.id = "A9B1"
    publish = asyncio.create_task(pub_server.publish(envelope))

    event = await client.receive_one_telemetry()
    await publish

    assert event is recorder.events[-1]
    assert event.seq == 73
    assert recorder.subscriber_empty_at_record == [True]
    published = subscriber.get_nowait()
    assert published is event
    assert published.seq == 73


@pytest.mark.asyncio
async def test_recorder_rejection_still_publishes_mutated_event(transport_fixture):
    client, ground_state, recorder, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    subscriber = ground_state.subscribe()
    recorder.subscriber = subscriber
    recorder.result = False
    publish = asyncio.create_task(
        pub_server.publish(
            task_event("case-1", "telemetry", 45, {"current_cell": "A7B1"})
        )
    )

    event = await client.receive_one_telemetry()
    await publish

    assert ground_state.snapshot(now_ms()).current_cell == "A7B1"
    assert recorder.events[-1] is event
    assert recorder.subscriber_empty_at_record == [True]
    assert subscriber.get_nowait() is event


@pytest.mark.asyncio
async def test_malformed_telemetry_is_ignored_without_updating_state(transport_fixture):
    client, ground_state, recorder, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    publish = asyncio.create_task(pub_server.publish_raw(b"\xffnot-protobuf"))

    assert await client.receive_one_telemetry() is None
    await publish
    assert ground_state.snapshot(now_ms()).current_cell is None
    assert recorder.events == []


@pytest.mark.asyncio
async def test_mission_load_preserves_existing_json_strings_and_command_mapping(
    transport_fixture,
):
    client, _, _, rep_server, _ = transport_fixture
    await client.send_mission_load(plan_for("case-1"))
    await client.send_control(GroundControlCommand.START, "case-1")
    await client.send_control(GroundControlCommand.STOP, "case-1")
    await client.send_control(GroundControlCommand.ARM_TARGETING, "case-1")
    await client.send_control(GroundControlCommand.DISARM_TARGETING, "case-1")

    mission = rep_server.requests[0].mission_load
    assert mission.metadata_json == '{ "keep": "spacing" }'
    assert mission.waypoints[0].payload_json == '{ "raw": true }'
    assert [item.control_command.type for item in rep_server.requests[1:]] == [
        MESSAGES.COMMAND_TYPE_START_MISSION,
        MESSAGES.COMMAND_TYPE_STOP_MISSION,
        MESSAGES.COMMAND_TYPE_ARM_TARGETING,
        MESSAGES.COMMAND_TYPE_RESET_TARGETING,
    ]
    assert rep_server.requests[0].sequence >= now_ms() << 20
    assert [item.sequence for item in rep_server.requests] == sorted(
        item.sequence for item in rep_server.requests
    )


def test_ground_control_command_is_a_string_enum():
    assert {item.value for item in GroundControlCommand} == {
        "start",
        "stop",
        "ping",
        "arm_targeting",
        "disarm_targeting",
    }
