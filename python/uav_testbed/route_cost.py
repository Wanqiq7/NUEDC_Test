from __future__ import annotations

from .mission_geometry import (
    LandingProfile,
    cell_code_center_cm,
    euclidean_distance_cm,
    terminal_cells_for_landing,
)


def _step_vector(route_from: str, route_to: str) -> tuple[int, int]:
    a_from = route_from.index("A")
    b_from = route_from.index("B")
    a_to = route_to.index("A")
    b_to = route_to.index("B")
    from_x = int(route_from[a_from + 1:b_from]) - 1
    from_y = int(route_from[b_from + 1:]) - 1
    to_x = int(route_to[a_to + 1:b_to]) - 1
    to_y = int(route_to[b_to + 1:]) - 1
    return to_x - from_x, to_y - from_y


def count_heading_changes(route: list[str]) -> int:
    heading_changes = 0
    previous_vector: tuple[int, int] | None = None
    for previous_cell, current_cell in zip(route, route[1:]):
        current_vector = _step_vector(previous_cell, current_cell)
        if previous_vector is not None and current_vector != previous_vector:
            heading_changes += 1
        previous_vector = current_vector
    return heading_changes


def estimate_route_cost(
    route: list[str],
    height: int = 7,
    turn_penalty_cm: float = 18.0,
    repeated_cell_penalty_cm: float = 6.0,
    landing_profile: LandingProfile | None = None,
    width: int = 9,
    no_fly_cells: set[str] | None = None,
) -> float:
    if len(route) <= 1:
        return 0.0

    distance_cost = 0.0
    visit_counts: dict[str, int] = {}
    for previous_cell, current_cell in zip(route, route[1:]):
        distance_cost += euclidean_distance_cm(
            cell_code_center_cm(previous_cell, height),
            cell_code_center_cm(current_cell, height),
        )
        visit_counts[current_cell] = visit_counts.get(current_cell, 0) + 1

    repeat_cost = sum(max(count - 1, 0) for count in visit_counts.values()) * repeated_cell_penalty_cm
    turn_cost = count_heading_changes(route) * turn_penalty_cm
    total_cost = distance_cost + repeat_cost + turn_cost

    if landing_profile is not None:
        terminal_cost = euclidean_distance_cm(
            cell_code_center_cm(route[-1], height),
            landing_profile.takeoff_anchor_cm,
        )
        total_cost += terminal_cost

        if no_fly_cells is None:
            no_fly_cells = set()
        landing_cells = terminal_cells_for_landing(
            width=width,
            height=height,
            no_fly_cells=no_fly_cells,
            landing_profile=landing_profile,
        )
        if route[-1] not in landing_cells:
            total_cost += 10_000.0

    return total_cost
