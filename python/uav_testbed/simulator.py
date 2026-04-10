from __future__ import annotations

from collections import Counter
from typing import Iterator

from .mission_planning import MissionPlan, build_mission_plan
from .models import Animal, CaseConfig, LandingConfig
from .mission_geometry import PointCm


def build_case_from_dict(raw_case: dict) -> CaseConfig:
    animals = tuple(
        Animal(cell=item["cell"], name=item["name"], count=int(item["count"]))
        for item in raw_case.get("animals", [])
    )
    raw_landing = raw_case.get("landing")
    landing = None
    if raw_landing is not None:
        landing = LandingConfig(
            takeoff_anchor_cm=PointCm(
                x_cm=float(raw_landing["takeoff_anchor_cm"][0]),
                y_cm=float(raw_landing["takeoff_anchor_cm"][1]),
            ),
            cruise_height_cm=float(raw_landing["cruise_height_cm"]),
            descent_angle_deg=float(raw_landing["descent_angle_deg"]),
            touchdown_radius_cm=float(raw_landing["touchdown_radius_cm"]),
            preferred_heading_deg=float(raw_landing.get("preferred_heading_deg", 45.0)),
        )
    return CaseConfig(
        case_id=raw_case["case_id"],
        start_cell=raw_case["start_cell"],
        no_fly_cells=tuple(raw_case.get("no_fly_cells", [])),
        tick_interval_ms=int(raw_case.get("tick_interval_ms", 100)),
        animals=animals,
        return_to_start=bool(raw_case.get("return_to_start", False)),
        landing=landing,
    )


def simulate_messages(case: CaseConfig, mission_plan: MissionPlan | None = None) -> Iterator[dict]:
    config_payload: MissionPlan
    if mission_plan is None:
        config_payload = build_mission_plan(case)
    else:
        route_override = mission_plan.get("route")
        if not route_override:
            config_payload = build_mission_plan(case)
        else:
            config_payload = mission_plan
    route = config_payload["route"]
    animals_by_cell = {animal.cell: animal for animal in case.animals}
    totals: Counter[str] = Counter()

    yield config_payload

    visited_cells: set[str] = set()
    reported_detection_cells: set[str] = set()
    for step_index, cell in enumerate(route):
        visited_cells.add(cell)
        yield {
            "message_type": "telemetry",
            "step": step_index,
            "cell": cell,
            "tick_interval_ms": case.tick_interval_ms,
            "visited_cells": len(visited_cells),
        }
        animal = animals_by_cell.get(cell)
        if animal is not None and cell not in reported_detection_cells:
            reported_detection_cells.add(cell)
            totals[animal.name] += animal.count
            yield {
                "message_type": "detection",
                "cell": animal.cell,
                "animal_name": animal.name,
                "count": animal.count,
            }

    yield {
        "message_type": "summary",
        "totals": dict(totals),
        "visited_cells": len(visited_cells),
    }
