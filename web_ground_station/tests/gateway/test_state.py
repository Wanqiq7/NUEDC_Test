import asyncio
import logging

from nuedc_web_gateway.models import AckSnapshot
from nuedc_web_gateway.state import (
    COMMAND_FAILURES_OFFLINE,
    COMMAND_HEARTBEAT_MS,
    PID_TTL_MS,
    TELEMETRY_TTL_MS,
    GroundState,
)


def active_state(task_id: str = "active") -> GroundState:
    state = GroundState()
    state.apply_plan(
        {
            "task_id": task_id,
            "message_type": "task_plan",
            "waypoints": [{"id": "A9B1"}],
        },
        100,
    )
    return state


def test_models_use_snake_case_wire_fields():
    ack = AckSnapshot(
        ok=True,
        message="loaded",
        task_id="active",
        mission_loaded=True,
        mission_running=False,
        last_accepted_sequence=7,
        vision_armed=True,
    )

    assert ack.model_dump() == {
        "ok": True,
        "message": "loaded",
        "task_id": "active",
        "mission_loaded": True,
        "mission_running": False,
        "last_accepted_sequence": 7,
        "vision_armed": True,
    }


def test_other_task_event_does_not_change_snapshot():
    state = active_state()
    state.apply_task_event(
        task_id="old",
        event="telemetry",
        seq=2,
        timestamp_ms=101,
        payload={"current_cell": "A1B1"},
    )

    assert state.snapshot(102).current_cell is None


def test_latest_telemetry_replaces_intermediate_frame():
    state = active_state()
    event = state.apply_task_event(
        "active",
        "telemetry",
        1,
        101,
        {"current_cell": "A8B1", "visited_cells": 3},
    )
    state.apply_task_event("active", "telemetry", 2, 102, {"current_cell": "A7B1"})

    snapshot = state.snapshot(103)
    assert event is not None
    assert event.payload["visited_count"] == 3
    assert snapshot.current_cell == "A7B1"
    assert snapshot.telemetry_link == "online"
    assert state.snapshot(102 + TELEMETRY_TTL_MS + 1).telemetry_link == "stale"


def test_detection_deduplicates_track_and_updates_totals():
    state = active_state()
    payload = {
        "track_id": "track-7",
        "cell_code": "A7B1",
        "animal_name": "wolf",
        "count": 1,
    }
    state.apply_task_event("active", "detection", 3, 103, payload)
    state.apply_task_event("active", "detection", 4, 104, payload)

    snapshot = state.snapshot(105)
    assert snapshot.detection_totals == {"wolf": 1}
    assert len(snapshot.recent_detections) == 1


def test_legacy_detections_without_track_id_use_event_sequence():
    state = active_state()
    payload = {"cell_code": "A7B1", "animal_name": "wolf", "count": 1}
    state.apply_task_event("active", "detection", 3, 103, payload)
    state.apply_task_event("active", "detection", 4, 104, payload)

    assert state.snapshot(105).detection_totals == {"wolf": 2}


def test_repeated_or_older_sequence_cannot_overwrite_current_state(caplog):
    caplog.set_level(logging.DEBUG, logger="nuedc_web_gateway.state")
    state = active_state()
    state.apply_task_event("active", "target_update", 9, 109, {"target_cell": "A5B2"})
    state.apply_task_event("active", "target_update", 9, 110, {"target_cell": "A1B1"})
    state.apply_task_event("active", "target_update", 8, 111, {"target_cell": "A2B1"})

    assert state.snapshot(112).target_update == {"target_cell": "A5B2"}
    assert "stale task event" in caplog.text.lower()


def test_summary_is_isolated_and_keeps_success_and_payload():
    state = active_state()
    state.apply_summary("old", 5, 105, False, {"error": "old failure"})
    assert state.snapshot(106).recent_summary is None

    state.apply_summary("active", 6, 106, False, {"error": "camera"})
    snapshot = state.snapshot(107)
    assert snapshot.recent_summary == {"success": False, "error": "camera"}
    assert snapshot.recent_error == {"error": "camera"}


def test_summary_marks_running_mission_complete():
    state = active_state()
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="running",
            task_id="active",
            mission_loaded=True,
            mission_running=True,
            last_accepted_sequence=1,
            vision_armed=True,
        ),
        101,
    )

    state.apply_summary("active", 2, 102, True, {})

    assert state.snapshot(103).mission_running is False


def test_touchdown_and_summary_move_current_position_to_takeoff_cell():
    state = GroundState()
    state.apply_plan(
        {
            "task_id": "active",
            "message_type": "task_plan",
            "metadata_json": '{"start_cell":"A9B1"}',
            "waypoints": [],
        },
        100,
    )
    event = state.apply_task_event(
        "active",
        "telemetry",
        1,
        101,
        {"waypoint_id": "touchdown", "current_cell": "A6B2"},
    )
    assert event is not None
    assert event.payload["current_cell"] == "A9B1"
    assert state.snapshot(102).current_cell == "A9B1"

    state.apply_task_event(
        "active", "telemetry", 2, 102, {"current_cell": "A6B2"}
    )
    state.apply_summary("active", 3, 103, True, {})
    assert state.snapshot(104).current_cell == "A9B1"


def test_old_ack_cannot_restore_running_after_newer_summary():
    state = active_state()

    def running_ack(sequence: int) -> AckSnapshot:
        return AckSnapshot(
            ok=True,
            message="running",
            task_id="active",
            mission_loaded=True,
            mission_running=True,
            last_accepted_sequence=sequence,
            vision_armed=True,
        )

    state.apply_ack(running_ack(10), 101)
    state.apply_summary("active", 20, 102, True, {})
    state.apply_ack(running_ack(9), 103)
    state.apply_ack(running_ack(10), 104)
    assert state.snapshot(105).mission_running is False

    state.apply_ack(running_ack(11), 106)
    assert state.snapshot(107).mission_running is True


def test_plan_switch_preserves_global_ack_sequence_watermark():
    state = active_state("first")
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="first",
            task_id="first",
            mission_loaded=True,
            mission_running=False,
            last_accepted_sequence=10,
            vision_armed=False,
        ),
        101,
    )
    state.apply_plan(
        {"task_id": "second", "message_type": "task_plan", "waypoints": []},
        102,
    )

    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="second",
            task_id="second",
            mission_loaded=True,
            mission_running=False,
            last_accepted_sequence=1,
            vision_armed=False,
        ),
        103,
    )

    assert state.snapshot(104).ack is not None
    assert state.snapshot(104).ack.message == "first"
    assert state.snapshot(104).task_sync_state == "mismatch"


def test_command_ack_and_telemetry_use_independent_sequence_domains():
    state = active_state()
    subscriber = state.subscribe()
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="timestamp based command",
            task_id="active",
            mission_loaded=True,
            mission_running=False,
            last_accepted_sequence=2**62,
            vision_armed=False,
        ),
        101,
    )

    ack_event = subscriber.get_nowait()
    telemetry_event = state.apply_task_event(
        "active", "telemetry", 1, 102, {"current_cell": "A7B1"}
    )

    assert state.snapshot(103).current_cell == "A7B1"
    assert telemetry_event is not None
    assert telemetry_event.seq > ack_event.seq
    assert telemetry_event.seq == state.snapshot(103).snapshot_seq


def test_mismatched_airborne_ack_is_explicit_without_claiming_display_plan_state():
    state = active_state("display-plan")
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="airborne running another task",
            task_id="airborne-task",
            mission_loaded=True,
            mission_running=True,
            last_accepted_sequence=10,
            vision_armed=True,
        ),
        101,
    )

    snapshot = state.snapshot(102)
    assert snapshot.task_sync_state == "mismatch"
    assert snapshot.airborne_task_id == "airborne-task"
    assert snapshot.airborne_mission_running is True
    assert snapshot.mission_loaded is False
    assert snapshot.mission_running is False
    assert snapshot.vision_armed is False


def test_ack_mirror_and_command_link_failure_threshold():
    state = active_state()
    successful = AckSnapshot(
        ok=True,
        message="pong",
        task_id="active",
        mission_loaded=True,
        mission_running=True,
        last_accepted_sequence=7,
        vision_armed=True,
    )
    state.apply_ack(successful, 200)
    snapshot = state.snapshot(201)
    assert snapshot.command_link == "online"
    assert snapshot.mission_loaded is True
    assert snapshot.mission_running is True
    assert snapshot.ack == successful

    failed = successful.model_copy(update={"ok": False, "message": "timeout"})
    for offset in range(1, COMMAND_FAILURES_OFFLINE + 1):
        state.apply_ack(
            failed.model_copy(update={"last_accepted_sequence": 7 + offset}),
            200 + offset,
        )
    assert state.snapshot(205).command_link == "offline"
    assert COMMAND_HEARTBEAT_MS == 2000


def test_plan_switch_resets_task_specific_state():
    state = active_state("first")
    state.apply_task_event("first", "telemetry", 2, 102, {"current_cell": "A7B1"})
    state.apply_task_event(
        "first",
        "detection",
        3,
        103,
        {"track_id": "t1", "animal_name": "wolf", "count": 2},
    )

    state.apply_plan(
        {"task_id": "second", "message_type": "task_plan", "waypoints": []},
        104,
    )
    snapshot = state.snapshot(105)
    assert snapshot.active_task_id == "second"
    assert snapshot.current_cell is None
    assert snapshot.detection_totals == {}
    assert snapshot.mission_loaded is False


def test_ten_thousand_telemetry_frames_keep_bounded_state_and_subscriber_queue():
    state = active_state()
    subscriber = state.subscribe(maxsize=64)
    for seq in range(1, 10_001):
        state.apply_task_event(
            "active",
            "telemetry",
            seq,
            100 + seq,
            {"current_cell": "A7B1", "visited_cells": seq},
        )

    assert subscriber.qsize() <= 64
    assert state.snapshot(10_101).visited_count == 10_000


def test_full_subscriber_evicts_telemetry_to_retain_critical_event():
    state = active_state()
    subscriber = state.subscribe(maxsize=2)
    state.apply_task_event("active", "telemetry", 1, 101, {"visited_cells": 1})
    state.apply_task_event("active", "telemetry", 2, 102, {"visited_cells": 2})
    state.apply_task_event(
        "active", "detection", 3, 103, {"animal_name": "wolf", "count": 1}
    )

    queued = [subscriber.get_nowait(), subscriber.get_nowait()]
    assert any(item.event == "detection" for item in queued)
    assert state.snapshot(104).detection_totals == {"wolf": 1}


def test_snapshot_sequence_is_gateway_owned_not_airborne_sequence():
    state = active_state()
    before = state.snapshot(100).snapshot_seq
    state.apply_task_event("active", "telemetry", 41, 101, {})
    first = state.snapshot(102)
    second = state.snapshot(103)

    assert first.snapshot_seq == before + 1
    assert second.snapshot_seq == first.snapshot_seq
    assert PID_TTL_MS == 500
    assert isinstance(state.subscribe(), asyncio.Queue)


def test_unsubscribe_stops_delivery_to_disconnected_client():
    state = active_state()
    subscriber = state.subscribe()

    state.unsubscribe(subscriber)
    state.apply_task_event("active", "telemetry", 1, 101, {})

    assert subscriber.empty()
