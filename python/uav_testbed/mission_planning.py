from __future__ import annotations

from typing import Iterable, TypedDict

from .models import CaseConfig
from .route_planner import plan_route


MAP_WIDTH = 9
MAP_HEIGHT = 7


class MissionPlan(TypedDict):
    message_type: str
    case_id: str
    start_cell: str
    no_fly_cells: list[str]
    route: list[str]
    terminal_cell: str
    landing_enabled: bool
    descent_angle_deg: float | None
    takeoff_anchor_x_cm: float | None
    takeoff_anchor_y_cm: float | None


def build_mission_plan(
    case: CaseConfig,
    override_no_fly_cells: Iterable[str] | None = None,
) -> MissionPlan:
    no_fly_cells = tuple(override_no_fly_cells) if override_no_fly_cells is not None else case.no_fly_cells
    landing_profile = case.landing.to_profile() if case.landing is not None else None
    route = plan_route(
        width=MAP_WIDTH,
        height=MAP_HEIGHT,
        start_cell=case.start_cell,
        no_fly_cells=set(no_fly_cells),
        end_cell=case.start_cell if case.return_to_start else None,
        require_cycle=case.return_to_start,
        mission_mode="time_optimal_open" if landing_profile is not None else "legacy",
        landing_profile=landing_profile,
    )
    if not route:
        raise ValueError(f"Route planner returned empty route for case {case.case_id}")
    landing_enabled = case.landing is not None
    return {
        "message_type": "config",
        "case_id": case.case_id,
        "start_cell": case.start_cell,
        "no_fly_cells": list(no_fly_cells),
        "route": route,
        "terminal_cell": route[-1],
        "landing_enabled": landing_enabled,
        "descent_angle_deg": case.landing.descent_angle_deg if landing_enabled else None,
        "takeoff_anchor_x_cm": case.landing.takeoff_anchor_cm.x_cm if landing_enabled else None,
        "takeoff_anchor_y_cm": case.landing.takeoff_anchor_cm.y_cm if landing_enabled else None,
    }
