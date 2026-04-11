import json
import pathlib
import sys
import tempfile
import threading
import time
import unittest

import zmq


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PYTHON_ROOT = PROJECT_ROOT / "airborne"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from uav_testbed.command_server import (  # noqa: E402
    CommandServerState,
    build_ack_bytes,
    handle_control_command,
    mission_load_to_dict,
    serve_command_endpoint,
    store_mission_plan,
)
from uav_testbed.generated import messages_pb2  # noqa: E402


def make_plan() -> dict:
    return {
        "message_type": "config",
        "case_id": "demo",
        "start_cell": "A9B1",
        "no_fly_cells": ["A1B2", "A2B2", "A3B2"],
        "route": ["A9B1", "A8B1", "A7B1"],
        "terminal_cell": "A7B1",
        "landing_enabled": True,
        "descent_angle_deg": 45.0,
        "takeoff_anchor_x_cm": 450.0,
        "takeoff_anchor_y_cm": 350.0,
    }


class CommandServerTests(unittest.TestCase):
    def test_store_received_mission_plan_persists_runtime_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = pathlib.Path(tmpdir) / "active_mission_plan.json"
            ack = store_mission_plan(make_plan(), output_path)

            self.assertTrue(ack["success"])
            self.assertTrue(output_path.exists())
            stored = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(stored["no_fly_cells"], ["A1B2", "A2B2", "A3B2"])
            self.assertEqual(stored["route"], ["A9B1", "A8B1", "A7B1"])

    def test_store_rejects_route_intersecting_no_fly_zone(self) -> None:
        invalid_plan = make_plan()
        invalid_plan["route"] = ["A9B1", "A1B2"]

        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = pathlib.Path(tmpdir) / "active_mission_plan.json"
            ack = store_mission_plan(invalid_plan, output_path)

            self.assertFalse(ack["success"])
            self.assertIn("intersects", ack["message"])
            self.assertFalse(output_path.exists())

    def test_mission_load_to_dict_preserves_fields(self) -> None:
        config = messages_pb2.GridConfig()
        config.case_id = "demo"
        config.start_cell = "A9B1"
        config.no_fly_cells.extend(["A1B2", "A2B2"])
        config.route.extend(["A9B1", "A8B1"])
        config.terminal_cell = "A8B1"
        config.landing_enabled = True
        config.descent_angle_deg = 45.0
        config.takeoff_anchor_x_cm = 450.0
        config.takeoff_anchor_y_cm = 350.0

        plan = mission_load_to_dict(config)

        self.assertEqual(plan["case_id"], "demo")
        self.assertEqual(plan["no_fly_cells"], ["A1B2", "A2B2"])
        self.assertEqual(plan["route"], ["A9B1", "A8B1"])

    def test_server_replies_with_ack_and_persists_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = pathlib.Path(tmpdir) / "active_mission_plan.json"
            stop_event = threading.Event()
            endpoint = "tcp://127.0.0.1:5568"
            server_thread = threading.Thread(
                target=serve_command_endpoint,
                args=(endpoint, output_path, stop_event, 50),
                daemon=True,
            )
            server_thread.start()
            time.sleep(0.1)

            envelope = messages_pb2.Envelope()
            envelope.sequence = 1
            payload = envelope.mission_load
            payload.case_id = "demo"
            payload.start_cell = "A9B1"
            payload.no_fly_cells.extend(["A1B2", "A2B2", "A3B2"])
            payload.route.extend(["A9B1", "A8B1", "A7B1"])
            payload.terminal_cell = "A7B1"
            payload.landing_enabled = True
            payload.descent_angle_deg = 45.0
            payload.takeoff_anchor_x_cm = 450.0
            payload.takeoff_anchor_y_cm = 350.0

            context = zmq.Context.instance()
            socket = context.socket(zmq.REQ)
            socket.connect(endpoint)
            socket.send(envelope.SerializeToString())
            reply = socket.recv()
            socket.close(0)

            ack_envelope = messages_pb2.Envelope()
            ack_envelope.ParseFromString(reply)
            self.assertEqual(ack_envelope.WhichOneof("payload"), "ack")
            self.assertTrue(ack_envelope.ack.success)
            self.assertTrue(output_path.exists())

            stop_event.set()
            server_thread.join(timeout=1.0)

    def test_build_ack_bytes_encodes_ack_payload(self) -> None:
        payload = build_ack_bytes(True, "ok")
        envelope = messages_pb2.Envelope()
        envelope.ParseFromString(payload)

        self.assertEqual(envelope.WhichOneof("payload"), "ack")
        self.assertTrue(envelope.ack.success)
        self.assertEqual(envelope.ack.message, "ok")

    def test_handle_start_command_marks_server_state(self) -> None:
        state = CommandServerState()
        command = messages_pb2.ControlCommand()
        command.type = messages_pb2.COMMAND_TYPE_START_MISSION

        ack = handle_control_command(command, state)

        self.assertTrue(ack["success"])
        self.assertTrue(state.start_requested.is_set())
        self.assertFalse(state.stop_requested.is_set())

    def test_handle_ping_command_returns_pong(self) -> None:
        state = CommandServerState()
        command = messages_pb2.ControlCommand()
        command.type = messages_pb2.COMMAND_TYPE_PING

        ack = handle_control_command(command, state)

        self.assertTrue(ack["success"])
        self.assertEqual(ack["message"], "pong")


if __name__ == "__main__":
    unittest.main()
