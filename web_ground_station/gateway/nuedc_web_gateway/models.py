from dataclasses import dataclass
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field


@dataclass(frozen=True)
class PlanningRequest:
    case_path: str
    no_fly_cells: list[str]


class WebEvent(BaseModel):
    model_config = ConfigDict(populate_by_name=True, serialize_by_alias=True)

    schema_: Literal["nuedc.web.v1"] = Field(default="nuedc.web.v1", alias="schema")
    type: str
    seq: int
    timestamp_ms: int
    task_id: str | None = None
    event: str | None = None
    payload: dict[str, Any] = Field(default_factory=dict)

    @property
    def schema(self) -> Literal["nuedc.web.v1"]:
        return self.schema_


class AckSnapshot(BaseModel):
    ok: bool
    message: str
    task_id: str
    mission_loaded: bool
    mission_running: bool
    last_accepted_sequence: int
    vision_armed: bool


class GroundSnapshot(BaseModel):
    snapshot_seq: int = 0
    timestamp_ms: int
    active_task_id: str | None = None
    plan: dict[str, Any] | None = None
    command_link: str = "unknown"
    telemetry_link: str = "unknown"
    pid_link: str = "unknown"
    ack: AckSnapshot | None = None
    mission_loaded: bool = False
    mission_running: bool = False
    vision_armed: bool = False
    current_cell: str | None = None
    visited_count: int = 0
    detection_totals: dict[str, int] = Field(default_factory=dict)
    recent_detections: list[dict[str, Any]] = Field(default_factory=list)
    target_update: dict[str, Any] | None = None
    recent_summary: dict[str, Any] | None = None
    recent_error: dict[str, Any] | None = None
    recording_error: str = ""


class CommandResponse(BaseModel):
    ok: bool
    message: str
    error_code: str | None = None
    ack: AckSnapshot | None = None
