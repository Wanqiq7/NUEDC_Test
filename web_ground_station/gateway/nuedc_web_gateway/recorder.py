import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import queue
import threading
from typing import TextIO

from .models import WebEvent
from .state import GroundState

_HIGH_FREQUENCY_EVENTS = frozenset(
    {"telemetry", "pid_debug", "attitude", "motor_status"}
)
_STOP = object()


class JsonlRecorder:
    def __init__(
        self,
        directory: Path,
        state: GroundState,
        *,
        queue_size: int = 1024,
        max_file_bytes: int = 16 * 1024 * 1024,
    ) -> None:
        if queue_size <= 0 or max_file_bytes <= 0:
            raise ValueError("recorder bounds must be positive")
        self._directory = Path(directory)
        self._state = state
        self._queue: queue.Queue[WebEvent | object] = queue.Queue(maxsize=queue_size)
        self._max_file_bytes = max_file_bytes
        self._thread: threading.Thread | None = None
        self._file: TextIO | None = None
        self._file_bytes = 0
        self._part = 0
        self._session_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        self._failed = False
        self._failure_lock = threading.Lock()
        self.dropped_logs = 0

    async def start(self) -> None:
        if self._thread is None:
            self._thread = threading.Thread(
                target=self._writer_loop, name="nuedc-jsonl", daemon=True
            )
            self._thread.start()

    def record(self, event: WebEvent) -> bool:
        with self._failure_lock:
            if self._failed:
                self.dropped_logs += 1
                return False
        item = event.model_copy(deep=True)
        try:
            self._queue.put_nowait(item)
            return True
        except queue.Full:
            if event.event not in _HIGH_FREQUENCY_EVENTS and self._evict_telemetry():
                self.dropped_logs += 1
                self._queue.put_nowait(item)
                return True
            self.dropped_logs += 1
            if event.event not in _HIGH_FREQUENCY_EVENTS:
                self._state.set_recording_error(
                    "recording queue full; critical event was dropped"
                )
            return False

    async def stop(self) -> None:
        if self._thread is None:
            await self.start()
        thread = self._thread
        if thread is None:
            return
        while True:
            try:
                self._queue.put_nowait(_STOP)
                break
            except queue.Full:
                await asyncio.sleep(0.005)
        while thread.is_alive():
            await asyncio.sleep(0.005)
        thread.join(0)
        self._thread = None

    def _writer_loop(self) -> None:
        try:
            while True:
                item = self._queue.get()
                try:
                    if item is _STOP:
                        return
                    with self._failure_lock:
                        failed = self._failed
                    if failed:
                        self.dropped_logs += 1
                        continue
                    if isinstance(item, WebEvent):
                        line = json.dumps(
                            item.model_dump(mode="json"),
                            ensure_ascii=True,
                            separators=(",", ":"),
                        )
                        self._write_line(line)
                except (OSError, ValueError) as error:
                    self._fail(error)
                finally:
                    self._queue.task_done()
        finally:
            self._close_file()

    def _write_line(self, line: str) -> None:
        encoded_size = len(line.encode("utf-8")) + 1
        if self._file is None:
            self._open_part()
        elif (
            self._file_bytes and self._file_bytes + encoded_size > self._max_file_bytes
        ):
            self._close_file()
            self._open_part()
        if self._file is None:
            raise OSError("session log is not open")
        self._file.write(line + "\n")
        self._file.flush()
        self._file_bytes += encoded_size

    def _open_part(self) -> None:
        self._directory.mkdir(parents=True, exist_ok=True)
        self._part += 1
        path = self._directory / f"{self._session_name}-{self._part:04d}.jsonl"
        self._file = path.open("x", encoding="utf-8")
        self._file_bytes = 0

    def _close_file(self) -> None:
        if self._file is not None:
            self._file.flush()
            self._file.close()
            self._file = None

    def _fail(self, error: Exception) -> None:
        with self._failure_lock:
            self._failed = True
        self._state.set_recording_error(f"session recording failed: {error}")
        try:
            self._close_file()
        except OSError:
            pass

    def _evict_telemetry(self) -> bool:
        retained: list[WebEvent | object] = []
        removed = False
        while True:
            try:
                queued = self._queue.get_nowait()
            except queue.Empty:
                break
            self._queue.task_done()
            if (
                not removed
                and isinstance(queued, WebEvent)
                and queued.event in _HIGH_FREQUENCY_EVENTS
            ):
                removed = True
            else:
                retained.append(queued)
        for queued in retained:
            self._queue.put_nowait(queued)
        return removed
