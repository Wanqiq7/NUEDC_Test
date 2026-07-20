import asyncio
from collections import deque
from datetime import datetime, timezone
import json
from pathlib import Path
import threading
from typing import TextIO

from .models import WebEvent
from .state import GroundState

_HIGH_FREQUENCY_EVENTS = frozenset(
    {"telemetry", "pid_debug", "attitude", "motor_status"}
)


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
        self._capacity = queue_size
        self._buffer: deque[WebEvent] = deque()
        self._condition = threading.Condition()
        self._accepting = True
        self._stopping = False
        self._failed = False
        self._thread: threading.Thread | None = None
        self._max_file_bytes = max_file_bytes
        self._file: TextIO | None = None
        self._file_bytes = 0
        self._part = 0
        self._session_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        self.dropped_logs = 0

    async def start(self) -> None:
        with self._condition:
            if self._thread is not None or self._stopping:
                return
            self._thread = threading.Thread(
                target=self._writer_loop, name="nuedc-jsonl", daemon=True
            )
            self._thread.start()

    def record(self, event: WebEvent) -> bool:
        item = event.model_copy(deep=True)
        with self._condition:
            if not self._accepting or self._failed:
                self.dropped_logs += 1
                return False
            if len(self._buffer) >= self._capacity:
                if (
                    not self._is_high_frequency(item)
                    and self._evict_oldest_high_frequency()
                ):
                    self.dropped_logs += 1
                else:
                    self.dropped_logs += 1
                    self._condition.notify()
                    if not self._is_high_frequency(item):
                        self._state.set_recording_error(
                            "recording queue full; critical event was dropped"
                        )
                    return False
            self._buffer.append(item)
            self._condition.notify()
            return True

    async def stop(self) -> None:
        await self.start()
        with self._condition:
            self._accepting = False
            self._stopping = True
            thread = self._thread
            self._condition.notify_all()
        if thread is None:
            return
        while thread.is_alive():
            await asyncio.sleep(0.005)
        thread.join(0)

    def _writer_loop(self) -> None:
        try:
            while True:
                with self._condition:
                    while not self._buffer and not self._stopping:
                        self._condition.wait()
                    if not self._buffer:
                        return
                    item = self._buffer.popleft()
                    failed = self._failed
                if failed:
                    with self._condition:
                        self.dropped_logs += 1
                    continue
                try:
                    line = json.dumps(
                        item.model_dump(mode="json"),
                        ensure_ascii=True,
                        separators=(",", ":"),
                    )
                    self._write_line(line)
                except (OSError, TypeError, ValueError) as error:
                    self._fail(error)
        finally:
            try:
                self._close_file()
            except (OSError, ValueError) as error:
                self._fail(error)

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
        file = self._file
        self._file = None
        if file is not None:
            try:
                file.flush()
            finally:
                file.close()

    def _fail(self, error: Exception) -> None:
        with self._condition:
            self._failed = True
            self._accepting = False
            self._condition.notify_all()
        self._state.set_recording_error(f"session recording failed: {error}")
        try:
            self._close_file()
        except (OSError, ValueError):
            pass

    def _evict_oldest_high_frequency(self) -> bool:
        for index, queued in enumerate(self._buffer):
            if self._is_high_frequency(queued):
                del self._buffer[index]
                return True
        return False

    @staticmethod
    def _is_high_frequency(event: WebEvent) -> bool:
        return (
            event.event in _HIGH_FREQUENCY_EVENTS
            or event.type in _HIGH_FREQUENCY_EVENTS
        )
