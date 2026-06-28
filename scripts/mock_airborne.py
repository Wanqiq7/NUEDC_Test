#!/usr/bin/env python3
"""本地机载端 Mock，用于调试 Qt 地面站的 ZeroMQ/Protobuf 链路。"""

from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import ModuleType


REPO_ROOT = Path(__file__).resolve().parents[1]
PROTO_PATH = REPO_ROOT / "shared" / "proto" / "messages.proto"
GENERATED_DIR = Path(tempfile.gettempdir()) / "nuedc_mock_airborne_proto"


def load_messages_module() -> ModuleType:
    generated_file = GENERATED_DIR / "messages_pb2.py"
    if not generated_file.exists() or generated_file.stat().st_mtime < PROTO_PATH.stat().st_mtime:
        protoc = shutil.which("protoc")
        if protoc is None:
            raise RuntimeError("missing protoc; install protobuf-compiler")
        GENERATED_DIR.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [protoc, f"--proto_path={PROTO_PATH.parent}", f"--python_out={GENERATED_DIR}", str(PROTO_PATH)],
            check=True,
        )

    spec = importlib.util.spec_from_file_location("messages_pb2", generated_file)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load generated protobuf module: {generated_file}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def now_ms() -> int:
    return int(time.time() * 1000)


def compact_json(value: dict) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


class MockAirborne:
    def __init__(self, messages: ModuleType, args: argparse.Namespace) -> None:
        import zmq

        self.messages = messages
        self.args = args
        self.context = zmq.Context.instance()
        self.stop_event = threading.Event()
        self.state_lock = threading.Lock()
        self.task_id = args.task_id
        self.task_type = "h_problem"
        self.route = list(args.route)
        self.mission_loaded = False
        self.mission_running = False
        self.last_accepted_sequence = 0
        self.event_sequence = 1
        self.runtime_path = Path(args.runtime_path)

    def run(self) -> None:
        command_thread = threading.Thread(target=self.serve_commands, name="mock-airborne-command", daemon=True)
        telemetry_thread = threading.Thread(target=self.publish_telemetry, name="mock-airborne-telemetry", daemon=True)
        command_thread.start()
        telemetry_thread.start()

        print(
            f"mock airborne ready: PUB tcp://0.0.0.0:{self.args.telemetry_port}, "
            f"REP tcp://0.0.0.0:{self.args.command_port}"
        )
        print(f"runtime plan path: {self.runtime_path}")
        try:
            while not self.stop_event.is_set():
                time.sleep(0.2)
        except KeyboardInterrupt:
            print("stopping mock airborne...")
            self.stop_event.set()
        finally:
            command_thread.join(timeout=1.0)
            telemetry_thread.join(timeout=1.0)
            self.context.term()

    def serve_commands(self) -> None:
        import zmq

        socket = self.context.socket(zmq.REP)
        socket.linger = 0
        socket.rcvtimeo = 200
        socket.bind(f"tcp://0.0.0.0:{self.args.command_port}")
        try:
            while not self.stop_event.is_set():
                try:
                    payload = socket.recv()
                except zmq.Again:
                    continue

                reply = self.handle_command(payload)
                socket.send(reply.SerializeToString())
        finally:
            socket.close(0)

    def publish_telemetry(self) -> None:
        import zmq

        socket = self.context.socket(zmq.PUB)
        socket.linger = 0
        socket.bind(f"tcp://0.0.0.0:{self.args.telemetry_port}")
        time.sleep(self.args.pub_warmup_s)
        try:
            while not self.stop_event.is_set():
                with self.state_lock:
                    running = self.mission_running
                    loaded = self.mission_loaded
                    route = list(self.route)
                    task_id = self.task_id

                if loaded and (running or self.args.publish_when_idle):
                    for index, cell in enumerate(route):
                        if self.stop_event.is_set():
                            break
                        with self.state_lock:
                            if not self.mission_running and not self.args.publish_when_idle:
                                break
                        socket.send(self.build_task_event(task_id, "telemetry", index, cell).SerializeToString())
                        if index % max(1, self.args.detection_every) == 0:
                            socket.send(self.build_detection_event(task_id, index, cell).SerializeToString())
                        time.sleep(self.args.interval_s)

                    socket.send(self.build_summary(task_id, len(route)).SerializeToString())
                else:
                    time.sleep(0.2)
        finally:
            socket.close(0)

    def handle_command(self, payload: bytes):
        envelope = self.messages.Envelope()
        if not envelope.ParseFromString(payload):
            return self.build_ack(False, "invalid protobuf")

        payload_name = envelope.WhichOneof("payload")
        with self.state_lock:
            if envelope.sequence and envelope.sequence <= self.last_accepted_sequence:
                return self.build_ack_locked(False, "stale command")

            if payload_name == "mission_load":
                plan = envelope.mission_load
                if not plan.task_id or len(plan.waypoints) == 0:
                    return self.build_ack_locked(False, "invalid task plan")
                self.task_id = plan.task_id
                self.task_type = plan.task_type or "h_problem"
                self.route = [waypoint.id for waypoint in plan.waypoints]
                self.mission_loaded = True
                self.mission_running = False
                self.accept_sequence(envelope.sequence)
                self.write_task_plan(plan)
                print(f"mission_load accepted: task_id={self.task_id}, waypoints={len(self.route)}, seq={envelope.sequence}")
                return self.build_ack_locked(True, "task plan stored")

            if payload_name == "control_command":
                command = envelope.control_command
                if command.type == self.messages.COMMAND_TYPE_PING:
                    return self.build_ack_locked(True, "pong")
                if command.type == self.messages.COMMAND_TYPE_START_MISSION:
                    self.mission_running = True
                    self.accept_sequence(envelope.sequence)
                    print(f"start accepted: task_id={command.task_id or self.task_id}, seq={envelope.sequence}")
                    return self.build_ack_locked(True, "start accepted")
                if command.type == self.messages.COMMAND_TYPE_STOP_MISSION:
                    self.mission_running = False
                    self.accept_sequence(envelope.sequence)
                    print(f"stop accepted: task_id={command.task_id or self.task_id}, seq={envelope.sequence}")
                    return self.build_ack_locked(True, "stop accepted")
                return self.build_ack_locked(False, "unsupported command")

            return self.build_ack_locked(False, "unsupported payload")

    def accept_sequence(self, sequence: int) -> None:
        if sequence:
            self.last_accepted_sequence = sequence

    def build_ack(self, success: bool, message: str):
        with self.state_lock:
            return self.build_ack_locked(success, message)

    def build_ack_locked(self, success: bool, message: str):
        envelope = self.messages.Envelope()
        envelope.timestamp_ms = now_ms()
        ack = envelope.ack
        ack.success = success
        ack.message = message
        ack.task_id = self.task_id
        ack.mission_loaded = self.mission_loaded
        ack.mission_running = self.mission_running
        ack.last_accepted_sequence = self.last_accepted_sequence
        return envelope

    def build_task_event(self, task_id: str, event_type: str, index: int, cell: str):
        envelope = self.messages.Envelope()
        envelope.sequence = self.next_event_sequence()
        envelope.timestamp_ms = now_ms()
        event = envelope.task_event
        event.task_id = task_id
        event.event_type = event_type
        event.sequence_index = index
        event.waypoint_id = cell
        event.payload_json = compact_json({"current_cell": cell, "visited_cells": index + 1})
        return envelope

    def build_detection_event(self, task_id: str, index: int, cell: str):
        envelope = self.messages.Envelope()
        envelope.sequence = self.next_event_sequence()
        envelope.timestamp_ms = now_ms()
        event = envelope.task_event
        event.task_id = task_id
        event.event_type = "detection"
        event.sequence_index = index
        event.waypoint_id = cell
        event.payload_json = compact_json({"cell_code": cell, "animal_name": self.args.animal, "count": 1})
        return envelope

    def build_summary(self, task_id: str, visited_waypoints: int):
        envelope = self.messages.Envelope()
        envelope.sequence = self.next_event_sequence()
        envelope.timestamp_ms = now_ms()
        summary = envelope.task_summary
        summary.task_id = task_id
        summary.task_type = self.task_type
        summary.success = True
        summary.visited_waypoints = visited_waypoints
        summary.payload_json = compact_json({"totals": {self.args.animal: max(1, visited_waypoints // max(1, self.args.detection_every))}})
        return envelope

    def next_event_sequence(self) -> int:
        value = self.event_sequence
        self.event_sequence += 1
        return value

    def write_task_plan(self, plan) -> None:
        waypoints = []
        for waypoint in plan.waypoints:
            waypoints.append(
                {
                    "id": waypoint.id,
                    "sequence_index": waypoint.sequence_index,
                    "x": waypoint.x,
                    "y": waypoint.y,
                    "z": waypoint.z,
                    "action": waypoint.action,
                    "payload_json": waypoint.payload_json,
                }
            )
        document = {
            "message_type": "task_plan",
            "task_id": plan.task_id,
            "task_type": plan.task_type,
            "start_waypoint_id": plan.start_waypoint_id,
            "terminal_waypoint_id": plan.terminal_waypoint_id,
            "waypoints": waypoints,
            "metadata_json": plan.metadata_json,
        }
        self.runtime_path.parent.mkdir(parents=True, exist_ok=True)
        self.runtime_path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a local mock airborne endpoint for the NUEDC ground station.")
    parser.add_argument("--telemetry-port", type=int, default=5557, help="PUB port for task events and summaries.")
    parser.add_argument("--command-port", type=int, default=5558, help="REP port for mission_load/control_command.")
    parser.add_argument("--runtime-path", default=str(REPO_ROOT / "runtime" / "mock_airborne_active_mission_plan.json"))
    parser.add_argument("--task-id", default="mock-h-task")
    parser.add_argument("--route", nargs="+", default=["A1", "A2", "A3", "B3", "C3"], help="Fallback route before mission_load.")
    parser.add_argument("--animal", default="hare")
    parser.add_argument("--interval-s", type=float, default=0.5, help="Delay between telemetry waypoints.")
    parser.add_argument("--detection-every", type=int, default=3, help="Emit one detection every N telemetry events.")
    parser.add_argument("--pub-warmup-s", type=float, default=0.5, help="Wait after PUB bind before sending first event.")
    parser.add_argument("--publish-when-idle", action="store_true", help="Publish events even before START_MISSION.")
    parser.add_argument("--self-test", action="store_true", help="Run protocol self-test without binding ZeroMQ ports.")
    return parser.parse_args()


def run_self_test(messages: ModuleType, args: argparse.Namespace) -> None:
    runtime_path = Path(args.runtime_path)
    if runtime_path.exists():
        runtime_path.unlink()

    mock = MockAirborne(messages, args)

    load = messages.Envelope()
    load.sequence = 10
    load.timestamp_ms = now_ms()
    plan = load.mission_load
    plan.task_id = "mock-self-test"
    plan.task_type = "h_problem"
    plan.start_waypoint_id = "A1"
    plan.terminal_waypoint_id = "A3"
    for index, cell in enumerate(("A1", "A2", "A3")):
        waypoint = plan.waypoints.add()
        waypoint.id = cell
        waypoint.sequence_index = index
        waypoint.action = "navigate"
        waypoint.payload_json = compact_json({"cell": cell})

    load_ack = mock.handle_command(load.SerializeToString()).ack
    assert load_ack.success and load_ack.mission_loaded and not load_ack.mission_running, load_ack

    start = messages.Envelope()
    start.sequence = 11
    start.timestamp_ms = now_ms()
    start.control_command.type = messages.COMMAND_TYPE_START_MISSION
    start.control_command.task_id = "mock-self-test"
    start_ack = mock.handle_command(start.SerializeToString()).ack
    assert start_ack.success and start_ack.mission_running, start_ack

    stale = messages.Envelope()
    stale.sequence = 11
    stale.timestamp_ms = now_ms()
    stale.control_command.type = messages.COMMAND_TYPE_START_MISSION
    stale.control_command.task_id = "mock-self-test"
    stale_ack = mock.handle_command(stale.SerializeToString()).ack
    assert not stale_ack.success and stale_ack.message == "stale command" and stale_ack.mission_running, stale_ack

    stop = messages.Envelope()
    stop.sequence = 12
    stop.timestamp_ms = now_ms()
    stop.control_command.type = messages.COMMAND_TYPE_STOP_MISSION
    stop.control_command.task_id = "mock-self-test"
    stop_ack = mock.handle_command(stop.SerializeToString()).ack
    assert stop_ack.success and not stop_ack.mission_running, stop_ack

    stored = json.loads(runtime_path.read_text(encoding="utf-8"))
    assert stored["message_type"] == "task_plan"
    assert stored["task_id"] == "mock-self-test"
    assert len(stored["waypoints"]) == 3
    print("mock airborne self-test passed")


def main() -> int:
    try:
        args = parse_args()
        messages = load_messages_module()
        if args.self_test:
            run_self_test(messages, args)
        else:
            MockAirborne(messages, args).run()
        return 0
    except ModuleNotFoundError as error:
        if error.name == "google":
            print("missing Python protobuf runtime; install python3-protobuf", file=sys.stderr)
        elif error.name == "zmq":
            print("missing pyzmq; install python3-zmq", file=sys.stderr)
        else:
            print(str(error), file=sys.stderr)
        return 1
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
