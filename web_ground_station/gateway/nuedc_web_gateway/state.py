import asyncio
from collections import deque
from copy import deepcopy
import json
import logging
import threading
import time
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
_WEB_SEQUENCE_COUNTER_BITS = 10

logger = logging.getLogger(__name__)


class GroundState:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._subscribers: list[asyncio.Queue[WebEvent]] = []
        # A time-based boot epoch keeps Web sequence numbers increasing across
        # normal Gateway restarts. Airborne sequence numbers remain a separate
        # deduplication domain and are never exposed as Web event sequence IDs.
        self._snapshot_seq = (
            time.time_ns() // 1_000_000
        ) << _WEB_SEQUENCE_COUNTER_BITS
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
        self._detection_history: deque[dict[str, Any]] = deque()
        self._detection_keys: set[tuple[str, str | int]] = set()
        self._target_update: dict[str, Any] | None = None
        self._recent_summary: dict[str, Any] | None = None
        self._recent_error: dict[str, Any] | None = None
        self._recent_error_source: str | None = None
        self._recording_error = ""
        self._resyncing = False

    @property
    def recording_error(self) -> str:
        with self._lock:
            return self._recording_error

    def set_recording_error(self, message: str) -> None:
        with self._lock:
            self._recording_error = message
            self._advance_snapshot()

    def set_recent_error(self, message: str) -> None:
        with self._lock:
            self._recent_error = {"message": message}
            self._recent_error_source = "command"
            self._advance_snapshot()

    def mark_resyncing(self) -> None:
        with self._lock:
            self._resyncing = True
            self._advance_snapshot()

    def subscribe(self, maxsize: int = 64) -> asyncio.Queue[WebEvent]:
        if maxsize <= 0:
            raise ValueError("subscriber maxsize must be positive")
        queue: asyncio.Queue[WebEvent] = asyncio.Queue(maxsize=maxsize)
        with self._lock:
            self._subscribers.append(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue[WebEvent]) -> None:
        with self._lock:
            try:
                self._subscribers.remove(queue)
            except ValueError:
                pass

    def apply_plan(
        self,
        plan: Mapping[str, Any],
        timestamp_ms: int,
        *,
        seq: int | None = None,
        publish: bool = True,
    ) -> WebEvent:
        plan_copy = deepcopy(dict(plan))
        task_id = plan_copy.get("task_id")
        if not isinstance(task_id, str) or not task_id:
            raise ValueError("plan task_id must be a non-empty string")
        with self._lock:
            self._active_task_id = task_id
            self._plan = plan_copy
            self._reset_task_state()
            if self._ack is not None:
                self._ack = self._ack.model_copy(
                    update={
                        "mission_loaded": False,
                        "mission_running": False,
                        "vision_armed": False,
                    }
                )
            snapshot_seq = self._advance_snapshot()
            event = WebEvent(
                type="task_plan",
                seq=snapshot_seq,
                timestamp_ms=timestamp_ms,
                task_id=task_id,
                payload=plan_copy,
            )
            if publish:
                self._publish(event)
            return event

    def apply_ack(self, ack: AckSnapshot, timestamp_ms: int) -> None:
        with self._lock:
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
                self._resyncing = False
                self._last_command_success_ms = timestamp_ms
                self._command_failures = 0
                if self._recent_error_source == "command":
                    self._recent_error = None
                    self._recent_error_source = None
            else:
                self._command_failures += 1
                self._recent_error = {"message": ack.message}
                self._recent_error_source = "command"
            seq = self._advance_snapshot()
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
        *,
        publish: bool = True,
    ) -> WebEvent | None:
        payload_copy = deepcopy(dict(payload))
        with self._lock:
            if not self._accept_sequence(task_id, seq, "task event"):
                return None
            if event == "telemetry":
                if payload_copy.get("waypoint_id") == "touchdown":
                    landing_cell = self._start_cell_from_plan()
                    if landing_cell is not None:
                        payload_copy["current_cell"] = landing_cell
                self._apply_telemetry(payload_copy, timestamp_ms)
                payload_copy["visited_count"] = self._visited_count
            elif event == "pid_debug":
                self._last_pid_ms = timestamp_ms
            elif event == "detection":
                self._apply_detection(task_id, seq, payload_copy)
            elif event == "target_update":
                self._target_update = payload_copy
            elif event == "error":
                self._recent_error = payload_copy
                self._recent_error_source = "task"
            web_seq = self._advance_snapshot()
            web_event = WebEvent(
                type="task_event",
                seq=web_seq,
                timestamp_ms=timestamp_ms,
                task_id=task_id,
                event=event,
                payload=payload_copy,
            )
            if publish:
                self._publish(web_event)
            return web_event

    def apply_summary(
        self,
        task_id: str,
        seq: int,
        timestamp_ms: int,
        success: bool,
        payload: Mapping[str, Any],
        *,
        publish: bool = True,
    ) -> WebEvent | None:
        payload_copy = deepcopy(dict(payload))
        with self._lock:
            if not self._accept_sequence(task_id, seq, "summary"):
                return None
            summary = {"success": success, **payload_copy}
            self._recent_summary = summary
            if success:
                landing_cell = self._start_cell_from_plan()
                if landing_cell is not None:
                    self._current_cell = landing_cell
            if self._ack is not None:
                self._ack = self._ack.model_copy(update={"mission_running": False})
            if not success:
                self._recent_error = payload_copy
                self._recent_error_source = "task"
            web_seq = self._advance_snapshot()
            event = WebEvent(
                type="task_summary",
                seq=web_seq,
                timestamp_ms=timestamp_ms,
                task_id=task_id,
                event="summary",
                payload=summary,
            )
            if publish:
                self._publish(event)
            return event

    def publish_event(self, event: WebEvent) -> None:
        with self._lock:
            self._publish(event)

    def detection_history(self, task_id: str | None = None) -> list[dict[str, Any]]:
        with self._lock:
            entries = (
                item
                for item in self._detection_history
                if task_id is None or item.get("task_id") == task_id
            )
            return deepcopy(list(entries))

    def snapshot(self, now_ms: int) -> GroundSnapshot:
        with self._lock:
            task_matches = (
                self._ack is not None
                and self._active_task_id is not None
                and self._ack.task_id == self._active_task_id
            )
            sync_state = (
                "unconfirmed"
                if self._ack is None
                else "matched"
                if task_matches
                else "mismatch"
            )
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
                task_sync_state=sync_state,
                airborne_task_id=self._ack.task_id or None if self._ack else None,
                airborne_mission_loaded=self._ack.mission_loaded if self._ack else False,
                airborne_mission_running=self._ack.mission_running if self._ack else False,
                mission_loaded=self._ack.mission_loaded if task_matches else False,
                mission_running=self._ack.mission_running if task_matches else False,
                vision_armed=self._ack.vision_armed if task_matches else False,
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
        self._last_telemetry_ms = None
        self._last_pid_ms = None
        self._highest_event_seq = -1
        self._current_cell = None
        self._visited_count = 0
        self._detection_totals.clear()
        self._recent_detections.clear()
        self._detection_keys.clear()
        self._target_update = None
        self._recent_summary = None
        self._recent_error = None
        self._recent_error_source = None

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

    def _start_cell_from_plan(self) -> str | None:
        if self._plan is None:
            return None
        metadata_raw = self._plan.get("metadata_json")
        if not isinstance(metadata_raw, str):
            return None
        try:
            metadata = json.loads(metadata_raw)
        except (TypeError, ValueError, json.JSONDecodeError):
            return None
        start_cell = metadata.get("start_cell") if isinstance(metadata, dict) else None
        return start_cell if isinstance(start_cell, str) else None

    def _apply_detection(self, task_id: str, seq: int, payload: dict[str, Any]) -> None:
        track_id = payload.get("track_id")
        key: tuple[str, str | int]
        key = (task_id, track_id) if isinstance(track_id, str) else (task_id, seq)
        if key in self._detection_keys:
            return
        self._detection_keys.add(key)
        self._recent_detections.append(payload)
        self._detection_history.append({**payload, "task_id": task_id})
        animal_name = payload.get("animal_name")
        count = payload.get("count", 1)
        if isinstance(animal_name, str) and animal_name:
            valid_count = (
                count if isinstance(count, int) and not isinstance(count, bool) else 1
            )
            self._detection_totals[animal_name] = self._detection_totals.get(
                animal_name, 0
            ) + max(0, valid_count)

    def _advance_snapshot(self) -> int:
        self._snapshot_seq += 1
        return self._snapshot_seq

    def _command_link(self, now_ms: int) -> str:
        if self._resyncing:
            return "resyncing"
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
