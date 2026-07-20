import asyncio
import json
from pathlib import Path
import time

import pytest

from nuedc_web_gateway.models import WebEvent
from nuedc_web_gateway.recorder import JsonlRecorder
from nuedc_web_gateway.state import GroundState


def event(seq: int, event_name: str = "telemetry") -> WebEvent:
    return WebEvent(
        type="task_event",
        seq=seq,
        timestamp_ms=100 + seq,
        task_id="active",
        event=event_name,
        payload={"value": seq},
    )


def read_session_lines(directory: Path) -> list[dict]:
    documents = []
    for path in sorted(directory.glob("*.jsonl")):
        documents.extend(json.loads(line) for line in path.read_text().splitlines())
    return documents


@pytest.mark.asyncio
async def test_records_three_events_as_compact_jsonl_in_sequence(tmp_path):
    recorder = JsonlRecorder(tmp_path, GroundState(), queue_size=8)
    await recorder.start()

    assert recorder.record(event(1)) is True
    assert recorder.record(event(2, "detection")) is True
    assert recorder.record(event(3, "summary")) is True
    await asyncio.wait_for(recorder.stop(), timeout=1)

    paths = list(tmp_path.glob("*.jsonl"))
    assert len(paths) == 1
    lines = paths[0].read_text().splitlines()
    assert [json.loads(line)["seq"] for line in lines] == [1, 2, 3]
    assert all(": " not in line for line in lines)


@pytest.mark.asyncio
async def test_full_queue_record_never_blocks_and_counts_dropped_high_frequency_logs(
    tmp_path,
):
    recorder = JsonlRecorder(tmp_path, GroundState(), queue_size=1)

    started = time.monotonic()
    assert recorder.record(event(1)) is True
    assert recorder.record(event(2)) is False
    elapsed = time.monotonic() - started

    assert elapsed < 0.05
    assert recorder.dropped_logs == 1


@pytest.mark.asyncio
async def test_critical_log_evicts_queued_telemetry_when_full(tmp_path):
    recorder = JsonlRecorder(tmp_path, GroundState(), queue_size=1)
    assert recorder.record(event(1, "telemetry")) is True

    assert recorder.record(event(2, "detection")) is True
    await recorder.start()
    await recorder.stop()

    assert [item["seq"] for item in read_session_lines(tmp_path)] == [2]
    assert recorder.dropped_logs == 1


@pytest.mark.asyncio
async def test_unretainable_critical_log_marks_recording_degraded(tmp_path):
    state = GroundState()
    recorder = JsonlRecorder(tmp_path, state, queue_size=1)
    assert recorder.record(event(1, "detection")) is True

    assert recorder.record(event(2, "summary")) is False

    assert recorder.dropped_logs == 1
    assert "queue" in state.snapshot(100).recording_error.lower()


@pytest.mark.asyncio
async def test_rotates_numbered_jsonl_parts_and_preserves_sequence(tmp_path):
    recorder = JsonlRecorder(tmp_path, GroundState(), queue_size=16, max_file_bytes=128)
    await recorder.start()
    for seq in range(1, 7):
        assert recorder.record(event(seq, "detection")) is True
    await recorder.stop()

    paths = sorted(tmp_path.glob("*.jsonl"))
    assert len(paths) > 1
    assert all(
        path.stem.endswith(f"-{index:04d}") for index, path in enumerate(paths, 1)
    )
    assert [item["seq"] for item in read_session_lines(tmp_path)] == list(range(1, 7))


@pytest.mark.asyncio
async def test_write_failure_marks_state_and_later_records_are_dropped(
    tmp_path, monkeypatch
):
    state = GroundState()
    recorder = JsonlRecorder(tmp_path, state, queue_size=4)

    def fail_open(*args, **kwargs):
        raise OSError("disk unavailable")

    monkeypatch.setattr(Path, "open", fail_open)
    await recorder.start()
    assert recorder.record(event(1, "detection")) is True
    await asyncio.wait_for(recorder.stop(), timeout=1)

    snapshot = state.snapshot(200)
    assert "disk unavailable" in snapshot.recording_error
    assert recorder.record(event(2, "detection")) is False


@pytest.mark.asyncio
async def test_slow_writer_does_not_block_record_call(tmp_path, monkeypatch):
    recorder = JsonlRecorder(tmp_path, GroundState(), queue_size=4)
    await recorder.start()
    real_write = recorder._write_line

    def slow_write(line: str):
        time.sleep(0.1)
        return real_write(line)

    monkeypatch.setattr(recorder, "_write_line", slow_write)
    started = time.monotonic()
    assert recorder.record(event(1)) is True
    elapsed = time.monotonic() - started
    await recorder.stop()

    assert elapsed < 0.05
