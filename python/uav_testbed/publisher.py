from __future__ import annotations

import time

import zmq

from .generated import messages_pb2


def _build_envelope(sequence: int, message: dict) -> messages_pb2.Envelope:
    envelope = messages_pb2.Envelope()
    envelope.sequence = sequence
    envelope.timestamp_ms = int(time.time() * 1000)

    message_type = message["message_type"]
    if message_type == "config":
        payload = envelope.grid_config
        payload.case_id = message["case_id"]
        payload.start_cell = message["start_cell"]
        payload.no_fly_cells.extend(message["no_fly_cells"])
        payload.route.extend(message["route"])
        payload.terminal_cell = message.get("terminal_cell", "")
        payload.landing_enabled = bool(message.get("landing_enabled", False))
        payload.descent_angle_deg = float(message.get("descent_angle_deg") or 0.0)
        payload.takeoff_anchor_x_cm = float(message.get("takeoff_anchor_x_cm") or 0.0)
        payload.takeoff_anchor_y_cm = float(message.get("takeoff_anchor_y_cm") or 0.0)
    elif message_type == "telemetry":
        payload = envelope.telemetry
        payload.current_cell = message["cell"]
        payload.step_index = int(message["step"])
        payload.visited_cells = int(message["visited_cells"])
    elif message_type == "detection":
        payload = envelope.animal_detection
        payload.cell_code = message["cell"]
        payload.animal_name = message["animal_name"]
        payload.count = int(message["count"])
    elif message_type == "summary":
        payload = envelope.mission_summary
        payload.visited_cells = int(message["visited_cells"])
        for animal_name, total_count in sorted(message["totals"].items()):
            total = payload.totals.add()
            total.animal_name = animal_name
            total.total_count = int(total_count)
    else:
        raise ValueError(f"unsupported message type: {message_type}")

    return envelope


class ZmqPublisher:
    def __init__(self, endpoint: str) -> None:
        self._context = zmq.Context.instance()
        self._socket = self._context.socket(zmq.PUB)
        self._socket.bind(endpoint)

    def close(self) -> None:
        self._socket.close(0)

    def publish(self, sequence: int, message: dict) -> None:
        envelope = _build_envelope(sequence, message)
        self._socket.send(envelope.SerializeToString())
