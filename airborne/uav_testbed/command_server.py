from __future__ import annotations

import argparse
import json
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import TypedDict

import zmq

from .generated import messages_pb2
from .mission_geometry import decode_cell
from .mission_planning import MissionPlan


class AckPayload(TypedDict):
    success: bool
    message: str


@dataclass
class CommandServerState:
    start_requested: threading.Event = field(default_factory=threading.Event)
    stop_requested: threading.Event = field(default_factory=threading.Event)


def mission_load_to_dict(config: messages_pb2.GridConfig) -> MissionPlan:
    return {
        "message_type": "config",
        "case_id": config.case_id,
        "start_cell": config.start_cell,
        "no_fly_cells": list(config.no_fly_cells),
        "route": list(config.route),
        "terminal_cell": config.terminal_cell,
        "landing_enabled": bool(config.landing_enabled),
        "descent_angle_deg": float(config.descent_angle_deg) if config.landing_enabled else None,
        "takeoff_anchor_x_cm": float(config.takeoff_anchor_x_cm) if config.landing_enabled else None,
        "takeoff_anchor_y_cm": float(config.takeoff_anchor_y_cm) if config.landing_enabled else None,
    }


def validate_mission_plan(plan: MissionPlan) -> tuple[bool, str]:
    required_string_fields = ("case_id", "start_cell", "terminal_cell")
    for field_name in required_string_fields:
        if not plan.get(field_name):
            return False, f"missing {field_name}"

    route = plan.get("route", [])
    if not route:
        return False, "route is empty"

    no_fly_cells = plan.get("no_fly_cells", [])
    try:
        decode_cell(plan["start_cell"])
        decode_cell(plan["terminal_cell"])
        for cell_code in no_fly_cells:
            decode_cell(cell_code)
        for cell_code in route:
            decode_cell(cell_code)
    except (KeyError, ValueError):
        return False, "invalid cell code"

    if set(route) & set(no_fly_cells):
        return False, "route intersects no_fly_cells"

    return True, ""


def store_mission_plan(plan: MissionPlan, output_path: Path) -> AckPayload:
    is_valid, message = validate_mission_plan(plan)
    if not is_valid:
        return {"success": False, "message": message}

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(plan, ensure_ascii=False), encoding="utf-8")
    return {"success": True, "message": "mission stored"}


def build_ack_bytes(success: bool, message: str) -> bytes:
    envelope = messages_pb2.Envelope()
    envelope.sequence = 0
    ack = envelope.ack
    ack.success = success
    ack.message = message
    return envelope.SerializeToString()


def handle_control_command(
    command: messages_pb2.ControlCommand,
    server_state: CommandServerState,
) -> AckPayload:
    if command.type == messages_pb2.COMMAND_TYPE_START_MISSION:
        server_state.stop_requested.clear()
        server_state.start_requested.set()
        return {"success": True, "message": "start accepted"}

    if command.type == messages_pb2.COMMAND_TYPE_STOP_MISSION:
        server_state.stop_requested.set()
        return {"success": True, "message": "stop accepted"}

    if command.type == messages_pb2.COMMAND_TYPE_PING:
        return {"success": True, "message": "pong"}

    return {"success": False, "message": "unsupported command"}


def serve_command_endpoint(
    endpoint: str,
    output_path: Path,
    stop_event: threading.Event | None = None,
    poll_timeout_ms: int = 100,
    server_state: CommandServerState | None = None,
) -> None:
    if server_state is None:
        server_state = CommandServerState()

    context = zmq.Context.instance()
    socket = context.socket(zmq.REP)
    socket.set(zmq.RCVTIMEO, poll_timeout_ms)
    socket.bind(endpoint)
    try:
        while stop_event is None or not stop_event.is_set():
            try:
                payload = socket.recv()
            except zmq.error.Again:
                continue

            envelope = messages_pb2.Envelope()
            if not envelope.ParseFromString(payload):
                socket.send(build_ack_bytes(False, "invalid protobuf"))
                continue

            if envelope.WhichOneof("payload") != "mission_load":
                if envelope.WhichOneof("payload") == "control_command":
                    ack = handle_control_command(envelope.control_command, server_state)
                    socket.send(build_ack_bytes(ack["success"], ack["message"]))
                    continue

                socket.send(build_ack_bytes(False, "unsupported payload"))
                continue

            mission_plan = mission_load_to_dict(envelope.mission_load)
            ack = store_mission_plan(mission_plan, output_path)
            socket.send(build_ack_bytes(ack["success"], ack["message"]))
    finally:
        socket.close(0)


def main() -> int:
    parser = argparse.ArgumentParser(description="H 题机载任务接收服务")
    parser.add_argument("--endpoint", default="tcp://0.0.0.0:5558", help="REP 监听地址")
    parser.add_argument(
        "--output",
        default="runtime/active_mission_plan.json",
        help="任务计划输出路径",
    )
    args = parser.parse_args()

    serve_command_endpoint(args.endpoint, Path(args.output), CommandServerState())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
