import asyncio
from enum import Enum
import json
import logging
import time
from typing import Any, Mapping, Protocol

from google.protobuf.message import DecodeError
import zmq
import zmq.asyncio

from .config import GatewayConfig
from .models import AckSnapshot, WebEvent
from .proto_runtime import load_messages_module
from .recorder import JsonlRecorder
from .state import COMMAND_HEARTBEAT_MS, GroundState


logger = logging.getLogger(__name__)
messages = load_messages_module()

_DEFAULT_TIMEOUT_MS = 1500
_DEFAULT_ATTEMPTS = 3
_DEFAULT_RETRY_DELAY_MS = 120
SEQUENCE_COUNTER_BITS = 20


def initial_command_sequence(timestamp_ms: int) -> int:
    """Return the sender-independent command epoch used by command clients."""
    return max(0, timestamp_ms) << SEQUENCE_COUNTER_BITS


class GroundControlCommand(str, Enum):
    START = "start"
    STOP = "stop"
    PING = "ping"
    ARM_TARGETING = "arm_targeting"
    DISARM_TARGETING = "disarm_targeting"


_PROTO_COMMAND_TYPES = {
    GroundControlCommand.START: messages.COMMAND_TYPE_START_MISSION,
    GroundControlCommand.STOP: messages.COMMAND_TYPE_STOP_MISSION,
    GroundControlCommand.PING: messages.COMMAND_TYPE_PING,
    GroundControlCommand.ARM_TARGETING: messages.COMMAND_TYPE_ARM_TARGETING,
    GroundControlCommand.DISARM_TARGETING: messages.COMMAND_TYPE_RESET_TARGETING,
}


class CommandTransport(Protocol):
    async def send(self, payload: bytes) -> bytes: ...


class _ZmqRequestTransport:
    def __init__(
        self,
        context: zmq.asyncio.Context,
        endpoint: str,
        timeout_ms: int,
    ) -> None:
        self._context = context
        self._endpoint = endpoint
        self._timeout_s = timeout_ms / 1000

    async def send(self, payload: bytes) -> bytes:
        socket = self._context.socket(zmq.REQ)
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, round(self._timeout_s * 1000))
        socket.setsockopt(zmq.RCVTIMEO, round(self._timeout_s * 1000))
        socket.connect(self._endpoint)
        try:
            await asyncio.wait_for(socket.send(payload), timeout=self._timeout_s)
            return await asyncio.wait_for(socket.recv(), timeout=self._timeout_s)
        except (asyncio.TimeoutError, zmq.Again) as error:
            raise TimeoutError("command ack timed out") from error
        finally:
            socket.close()


class AirborneClient:
    def __init__(
        self,
        config: GatewayConfig,
        state: GroundState,
        recorder: JsonlRecorder,
        *,
        context: zmq.asyncio.Context | None = None,
        timeout_ms: int = _DEFAULT_TIMEOUT_MS,
        max_attempts: int = _DEFAULT_ATTEMPTS,
        retry_delay_ms: int = _DEFAULT_RETRY_DELAY_MS,
    ) -> None:
        if timeout_ms <= 0 or max_attempts <= 0 or retry_delay_ms < 0:
            raise ValueError("invalid airborne command policy")
        self._config = config
        self._state = state
        self._recorder = recorder
        self._context = context if context is not None else zmq.asyncio.Context()
        self._owns_context = context is None
        self._timeout_ms = timeout_ms
        self._max_attempts = max_attempts
        self._retry_delay_s = retry_delay_ms / 1000
        self._command_lock = asyncio.Lock()
        self._sequence = initial_command_sequence(time.time_ns() // 1_000_000)
        self._transport: CommandTransport = _ZmqRequestTransport(
            self._context, config.command_endpoint, timeout_ms
        )
        self._telemetry = self._context.socket(zmq.SUB)
        self._telemetry.setsockopt(zmq.LINGER, 0)
        self._telemetry.setsockopt(zmq.SUBSCRIBE, b"")
        self._telemetry.connect(config.telemetry_endpoint)
        self._closed = False

    def replace_transport(self, transport: CommandTransport) -> None:
        self._transport = transport

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._telemetry.close()
        if self._owns_context:
            self._context.term()

    async def send_mission_load(self, plan: Mapping[str, Any]) -> AckSnapshot:
        envelope = self._new_envelope()
        self._fill_plan(envelope.mission_load, plan)
        return await self._send_reliable(envelope)

    async def send_control(
        self, command: GroundControlCommand, task_id: str
    ) -> AckSnapshot:
        if command is not GroundControlCommand.PING and not task_id.strip():
            raise ValueError("task_id is required for stateful commands")
        envelope = self._new_envelope()
        envelope.control_command.type = _PROTO_COMMAND_TYPES[command]
        envelope.control_command.task_id = task_id
        return await self._send_reliable(envelope)

    async def probe_once(self) -> AckSnapshot:
        active_task = self._state.snapshot(_now_ms()).active_task_id or ""
        return await self.send_control(GroundControlCommand.PING, active_task)

    async def heartbeat_loop(self, stop: asyncio.Event) -> None:
        interval_s = COMMAND_HEARTBEAT_MS / 1000
        while not stop.is_set():
            await self.probe_once()
            try:
                await asyncio.wait_for(stop.wait(), timeout=interval_s)
            except asyncio.TimeoutError:
                pass

    async def receive_one_telemetry(self) -> WebEvent | None:
        raw = await self._telemetry.recv()
        try:
            envelope = messages.Envelope.FromString(raw)
            event = self._apply_telemetry_envelope(envelope)
        except (DecodeError, ValueError, TypeError, json.JSONDecodeError) as error:
            logger.warning("ignoring invalid airborne telemetry: %s", error)
            return None
        if event is not None:
            self._recorder.record(event)
            self._state.publish_event(event)
        return event

    async def telemetry_loop(self, stop: asyncio.Event) -> None:
        while not stop.is_set():
            receive = asyncio.create_task(self.receive_one_telemetry())
            stopped = asyncio.create_task(stop.wait())
            done, pending = await asyncio.wait(
                {receive, stopped}, return_when=asyncio.FIRST_COMPLETED
            )
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
            if receive in done:
                try:
                    receive.result()
                except (zmq.ZMQError, asyncio.CancelledError):
                    if not stop.is_set():
                        raise

    def _new_envelope(self):
        self._sequence += 1
        return messages.Envelope(sequence=self._sequence, timestamp_ms=_now_ms())

    async def _send_reliable(self, envelope) -> AckSnapshot:
        async with self._command_lock:
            last_ack = self._failure_ack(envelope, "command was not attempted")
            payload = envelope.SerializeToString()
            for attempt in range(self._max_attempts):
                try:
                    reply = await self._transport.send(payload)
                    last_ack = self._parse_ack(reply, envelope)
                except (DecodeError, TimeoutError, zmq.ZMQError, ValueError) as error:
                    last_ack = self._failure_ack(envelope, str(error))
                if last_ack.ok:
                    break
                if attempt + 1 < self._max_attempts and self._retry_delay_s:
                    await asyncio.sleep(self._retry_delay_s)
            state_ack = last_ack
            if (
                last_ack.ok
                and envelope.WhichOneof("payload") == "control_command"
                and envelope.control_command.type == messages.COMMAND_TYPE_PING
                and last_ack.last_accepted_sequence < envelope.sequence
            ):
                # PING does not advance the airborne command watermark.
                state_ack = last_ack.model_copy(
                    update={"last_accepted_sequence": envelope.sequence}
                )
            self._state.apply_ack(state_ack, _now_ms())
            return last_ack

    def _parse_ack(self, payload: bytes, sent_envelope) -> AckSnapshot:
        envelope = messages.Envelope.FromString(payload)
        if envelope.WhichOneof("payload") != "ack":
            raise ValueError("command reply is not an ACK")
        ack = envelope.ack
        result = AckSnapshot(
            ok=ack.success,
            message=ack.message,
            task_id=ack.task_id,
            mission_loaded=ack.mission_loaded,
            mission_running=ack.mission_running,
            last_accepted_sequence=ack.last_accepted_sequence,
            vision_armed=ack.vision_armed,
        )
        if self._already_accepted(sent_envelope, result):
            return result.model_copy(
                update={"ok": True, "message": "command already accepted"}
            )
        return result

    @staticmethod
    def _already_accepted(envelope, ack: AckSnapshot) -> bool:
        if (
            ack.ok
            or envelope.sequence == 0
            or ack.last_accepted_sequence < envelope.sequence
        ):
            return False
        payload = envelope.WhichOneof("payload")
        if payload == "mission_load":
            return ack.mission_loaded and ack.task_id == envelope.mission_load.task_id
        if payload != "control_command":
            return False
        command = envelope.control_command
        is_ping = command.type == messages.COMMAND_TYPE_PING
        if not is_ping and command.task_id and ack.task_id != command.task_id:
            return False
        if command.type == messages.COMMAND_TYPE_START_MISSION:
            return ack.mission_running
        if command.type == messages.COMMAND_TYPE_STOP_MISSION:
            return not ack.mission_running
        if command.type == messages.COMMAND_TYPE_ARM_TARGETING:
            return ack.vision_armed
        if command.type == messages.COMMAND_TYPE_RESET_TARGETING:
            return not ack.vision_armed
        return is_ping

    @staticmethod
    def _failure_ack(envelope, message: str) -> AckSnapshot:
        payload = envelope.WhichOneof("payload")
        task_id = ""
        if payload == "mission_load":
            task_id = envelope.mission_load.task_id
        elif payload == "control_command":
            task_id = envelope.control_command.task_id
        return AckSnapshot(
            ok=False,
            message=message or "command failed",
            task_id=task_id,
            mission_loaded=False,
            mission_running=False,
            last_accepted_sequence=envelope.sequence,
            vision_armed=False,
        )

    @staticmethod
    def _fill_plan(target, plan: Mapping[str, Any]) -> None:
        target.task_id = str(plan.get("task_id", ""))
        target.task_type = str(plan.get("task_type", ""))
        target.start_waypoint_id = str(plan.get("start_waypoint_id", ""))
        target.terminal_waypoint_id = str(plan.get("terminal_waypoint_id", ""))
        target.metadata_json = str(plan.get("metadata_json", ""))
        for waypoint in plan.get("waypoints", []):
            item = target.waypoints.add()
            item.id = str(waypoint.get("id", ""))
            item.sequence_index = waypoint.get("sequence_index", 0)
            item.x = waypoint.get("x", 0.0)
            item.y = waypoint.get("y", 0.0)
            item.z = waypoint.get("z", 0.0)
            item.action = str(waypoint.get("action", ""))
            item.payload_json = str(waypoint.get("payload_json", ""))

    def _apply_telemetry_envelope(self, envelope) -> WebEvent | None:
        payload_type = envelope.WhichOneof("payload")
        timestamp_ms = envelope.timestamp_ms or _now_ms()
        if payload_type == "task_plan":
            plan = self._plan_to_dict(envelope.task_plan)
            return self._state.apply_plan(
                plan,
                timestamp_ms,
                seq=envelope.sequence,
                publish=False,
            )
        if payload_type == "task_event":
            item = envelope.task_event
            if not self._is_active_task(item.task_id):
                return None
            payload = _json_object(item.payload_json)
            payload["sequence_index"] = item.sequence_index
            if item.waypoint_id:
                payload["waypoint_id"] = item.waypoint_id
            return self._state.apply_task_event(
                item.task_id,
                item.event_type,
                envelope.sequence,
                timestamp_ms,
                payload,
                publish=False,
            )
        if payload_type == "task_summary":
            item = envelope.task_summary
            if not self._is_active_task(item.task_id):
                return None
            payload = {
                "success": item.success,
                "task_type": item.task_type,
                "visited_waypoints": item.visited_waypoints,
                **_json_object(item.payload_json),
            }
            return self._state.apply_summary(
                item.task_id,
                envelope.sequence,
                timestamp_ms,
                item.success,
                {key: value for key, value in payload.items() if key != "success"},
                publish=False,
            )
        return None

    def _is_active_task(self, task_id: str) -> bool:
        return self._state.snapshot(_now_ms()).active_task_id == task_id

    @staticmethod
    def _plan_to_dict(plan) -> dict[str, Any]:
        return {
            "message_type": "task_plan",
            "task_id": plan.task_id,
            "task_type": plan.task_type,
            "start_waypoint_id": plan.start_waypoint_id,
            "terminal_waypoint_id": plan.terminal_waypoint_id,
            "metadata_json": plan.metadata_json,
            "waypoints": [
                {
                    "id": item.id,
                    "sequence_index": item.sequence_index,
                    "x": item.x,
                    "y": item.y,
                    "z": item.z,
                    "action": item.action,
                    "payload_json": item.payload_json,
                }
                for item in plan.waypoints
            ],
        }


def _json_object(payload: str) -> dict[str, Any]:
    if not payload:
        return {}
    value = json.loads(payload)
    if not isinstance(value, dict):
        raise ValueError("payload_json must contain an object")
    return value


def _now_ms() -> int:
    return time.time_ns() // 1_000_000
