import asyncio
from collections import deque
from copy import deepcopy
import logging
import threading
from typing import Any, Mapping

from .models import AckSnapshot, GroundSnapshot, WebEvent

COMMAND_HEARTBEAT_MS = 2000
COMMAND_FAILURES_OFFLINE = 3
TELEMETRY_TTL_MS = 5000
PID_TTL_MS = 500

_HIGH_FREQUENCY_EVENTS = frozenset(
    {"telemetry", "pid_debug", "attitude", "motor_status"}
)
_RECENT_DETECTION_LIMIT = 100

logger = logging.getLogger(__name__)


class GroundState:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._subscribers: list[asyncio.Queue[WebEvent]] = []
        self._snapshot_seq = 0
        self._active_task_id: str | None = None
        self._plan: dict[str, Any] | None = None
        self._ack: AckSnapshot | None = None
        self._command_failures = 0
        self._last_command_success_ms: int | None = None
        self._last_telemetry_ms: int | None = None
        self._last_pid_ms: int | None = None
        self._highest_ack_sequence = -1
        self._highest_event_seq = -1
        self._current_cell: str | None = None
        self._visited_count = 0
        self._detection_totals: dict[str, int] = {}
        self._recent_detections: deque[dict[str, Any]] = deque(
            maxlen=_RECENT_DETECTION_LIMIT
        )
        self._detection_keys: set[tuple[str, str | int]] = set()
        self._target_update: dict[str, Any] | None = None
        self._recent_summary: dict[str, Any] | None = None
        self._recent_error: dict[str, Any] | None = None
        self._recording_error = ""

    @property
    def recording_error(self) -> str:
        with self._lock:
            return self._recording_error

    def set_recording_error(self, message: str) -> None:
        with self._lock:
            self._recording_error = message
            self._advance_snapshot()

    def subscribe(self, maxsize: int = 64) -> asyncio.Queue[WebEvent]:
        if maxsize <= 0:
            raise ValueError("subscriber maxsize must be positive")
        queue: asyncio.Queue[WebEvent] = asyncio.Queue(maxsize=maxsize)
        with self._lock:
            self._subscribers.append(queue)
        return queue

    def apply_plan(self, plan: Mapping[str, Any], timestamp_ms: int) -> None:
        plan_copy = deepcopy(dict(plan))
        task_id = plan_copy.get("task_id")
        if not isinstance(task_id, str) or not task_id:
            raise ValueError("plan task_id must be a non-empty string")
        with self._lock:
            switched = task_id != self._active_task_id
            self._active_task_id = task_id
            self._plan = plan_copy
            if switched:
                self._reset_task_state()
            seq = self._advance_snapshot()
            self._publish(
                WebEvent(
                    type="task_plan",
                    seq=seq,
                    timestamp_ms=timestamp_ms,
                    task_id=task_id,
                    payload=plan_copy,
                )
            )

    def apply_ack(self, ack: AckSnapshot, timestamp_ms: int) -> None:
        with self._lock:
            if self._active_task_id is not None and ack.task_id != self._active_task_id:
                logger.debug("ignoring ACK for inactive task %s", ack.task_id)
                return
            if ack.last_accepted_sequence <= self._highest_ack_sequence:
                logger.debug(
                    "ignoring stale ACK sequence %d (highest %d)",
                    ack.last_accepted_sequence,
                    self._highest_ack_sequence,
                )
                return
            self._highest_ack_sequence = ack.last_accepted_sequence
            self._ack = ack.model_copy(deep=True)
            if ack.ok:
                self._last_command_success_ms = timestamp_ms
                self._command_failures = 0
            else:
                self._command_failures += 1
                self._recent_error = {"message": ack.message}
            seq = self._advance_snapshot(ack.last_accepted_sequence)
            self._publish(
                WebEvent(
                    type="ack",
                    seq=seq,
                    timestamp_ms=timestamp_ms,
                    task_id=ack.task_id,
                    payload=ack.model_dump(),
                )
            )

    def apply_task_event(
        self,
        task_id: str,
        event: str,
        seq: int,
        timestamp_ms: int,
        payload: Mapping[str, Any],
    ) -> None:
        payload_copy = deepcopy(dict(payload))
        with self._lock:
            if not self._accept_sequence(task_id, seq, "task event"):
                return
            if event == "telemetry":
                self._apply_telemetry(payload_copy, timestamp_ms)
            elif event == "pid_debug":
                self._last_pid_ms = timestamp_ms
            elif event == "detection":
                self._apply_detection(task_id, seq, payload_copy)
            elif event == "target_update":
                self._target_update = payload_copy
            elif event == "error":
                self._recent_error = payload_copy
            self._advance_snapshot(seq)
            self._publish(
                WebEvent(
                    type="task_event",
                    seq=seq,
                    timestamp_ms=timestamp_ms,
                    task_id=task_id,
                    event=event,
                    payload=payload_copy,
                )
            )

    def apply_summary(
        self,
        task_id: str,
        seq: int,
        timestamp_ms: int,
        success: bool,
        payload: Mapping[str, Any],
    ) -> None:
        payload_copy = deepcopy(dict(payload))
        with self._lock:
            if not self._accept_sequence(task_id, seq, "summary"):
                return
            summary = {"success": success, **payload_copy}
            self._recent_summary = summary
            if self._ack is not None:
                self._ack = self._ack.model_copy(update={"mission_running": False})
            if not success:
                self._recent_error = payload_copy
            self._advance_snapshot(seq)
            self._publish(
                WebEvent(
                    type="task_summary",
                    seq=seq,
                    timestamp_ms=timestamp_ms,
                    task_id=task_id,
                    event="summary",
                    payload=summary,
                )
            )

    def snapshot(self, now_ms: int) -> GroundSnapshot:
        with self._lock:
            return GroundSnapshot(
                snapshot_seq=self._snapshot_seq,
                timestamp_ms=now_ms,
                active_task_id=self._active_task_id,
                plan=deepcopy(self._plan),
                command_link=self._command_link(now_ms),
                telemetry_link=self._ttl_link(
                    self._last_telemetry_ms, now_ms, TELEMETRY_TTL_MS
                ),
                pid_link=self._ttl_link(self._last_pid_ms, now_ms, PID_TTL_MS),
                ack=self._ack.model_copy(deep=True) if self._ack else None,
                mission_loaded=self._ack.mission_loaded if self._ack else False,
                mission_running=self._ack.mission_running if self._ack else False,
                vision_armed=self._ack.vision_armed if self._ack else False,
                current_cell=self._current_cell,
                visited_count=self._visited_count,
                detection_totals=dict(self._detection_totals),
                recent_detections=deepcopy(list(self._recent_detections)),
                target_update=deepcopy(self._target_update),
                recent_summary=deepcopy(self._recent_summary),
                recent_error=deepcopy(self._recent_error),
                recording_error=self._recording_error,
            )

    def _reset_task_state(self) -> None:
        self._ack = None
        self._command_failures = 0
        self._last_command_success_ms = None
        self._last_telemetry_ms = None
        self._last_pid_ms = None
        self._highest_ack_sequence = -1
        self._highest_event_seq = -1
        self._current_cell = None
        self._visited_count = 0
        self._detection_totals.clear()
        self._recent_detections.clear()
        self._detection_keys.clear()
        self._target_update = None
        self._recent_summary = None
        self._recent_error = None

    def _accept_sequence(self, task_id: str, seq: int, source: str) -> bool:
        if task_id != self._active_task_id:
            logger.debug("ignoring %s for inactive task %s", source, task_id)
            return False
        if seq <= self._highest_event_seq:
            logger.debug(
                "ignoring stale task event sequence %d (highest %d)",
                seq,
                self._highest_event_seq,
            )
            return False
        self._highest_event_seq = seq
        return True

    def _apply_telemetry(self, payload: dict[str, Any], timestamp_ms: int) -> None:
        self._last_telemetry_ms = timestamp_ms
        current_cell = payload.get("current_cell")
        if current_cell is None or isinstance(current_cell, str):
            self._current_cell = current_cell
        visited = payload.get("visited_cells")
        if isinstance(visited, int) and not isinstance(visited, bool):
            self._visited_count = max(0, visited)
        elif isinstance(visited, (list, tuple, set)):
            self._visited_count = len(visited)

    def _apply_detection(self, task_id: str, seq: int, payload: dict[str, Any]) -> None:
        track_id = payload.get("track_id")
        key: tuple[str, str | int]
        key = (task_id, track_id) if isinstance(track_id, str) else (task_id, seq)
        if key in self._detection_keys:
            return
        self._detection_keys.add(key)
        self._recent_detections.append(payload)
        animal_name = payload.get("animal_name")
        count = payload.get("count", 1)
        if isinstance(animal_name, str) and animal_name:
            valid_count = (
                count if isinstance(count, int) and not isinstance(count, bool) else 1
            )
            self._detection_totals[animal_name] = self._detection_totals.get(
                animal_name, 0
            ) + max(0, valid_count)

    def _advance_snapshot(self, seq: int | None = None) -> int:
        next_seq = self._snapshot_seq + 1
        self._snapshot_seq = max(next_seq, seq if seq is not None else next_seq)
        return self._snapshot_seq

    def _command_link(self, now_ms: int) -> str:
        if self._command_failures >= COMMAND_FAILURES_OFFLINE:
            return "offline"
        if self._last_command_success_ms is None:
            return "unknown"
        age = max(0, now_ms - self._last_command_success_ms)
        if age <= COMMAND_HEARTBEAT_MS:
            return "online"
        if age <= COMMAND_HEARTBEAT_MS * COMMAND_FAILURES_OFFLINE:
            return "stale"
        return "offline"

    @staticmethod
    def _ttl_link(last_update_ms: int | None, now_ms: int, ttl_ms: int) -> str:
        if last_update_ms is None:
            return "unknown"
        return "online" if max(0, now_ms - last_update_ms) <= ttl_ms else "stale"

    def _publish(self, event: WebEvent) -> None:
        for queue in self._subscribers:
            if not queue.full():
                queue.put_nowait(event)
                continue
            if self._evict_high_frequency(queue):
                queue.put_nowait(event)

    @staticmethod
    def _evict_high_frequency(queue: asyncio.Queue[WebEvent]) -> bool:
        retained: list[WebEvent] = []
        removed = False
        while not queue.empty():
            queued = queue.get_nowait()
            queue.task_done()
            if not removed and queued.event in _HIGH_FREQUENCY_EVENTS:
                removed = True
            else:
                retained.append(queued)
        for queued in retained:
            queue.put_nowait(queued)
        return removed
