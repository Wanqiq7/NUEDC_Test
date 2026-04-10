from __future__ import annotations

from dataclasses import dataclass
import math


FIELD_MARGIN_CM = 25.0
CELL_SIZE_CM = 50.0


@dataclass(frozen=True)
class PointCm:
    x_cm: float
    y_cm: float


@dataclass(frozen=True)
class LandingProfile:
    takeoff_anchor_cm: PointCm
    cruise_height_cm: float
    descent_angle_deg: float
    touchdown_radius_cm: float
    preferred_heading_deg: float = 45.0
    heading_tolerance_deg: float = 35.0


DEFAULT_SAMPLE_LANDING_PROFILE = LandingProfile(
    takeoff_anchor_cm=PointCm(x_cm=450.0, y_cm=350.0),
    cruise_height_cm=120.0,
    descent_angle_deg=45.0,
    touchdown_radius_cm=18.0,
)


def decode_cell(cell_code: str) -> tuple[int, int]:
    a_index = cell_code.index("A")
    b_index = cell_code.index("B")
    return int(cell_code[a_index + 1:b_index]) - 1, int(cell_code[b_index + 1:]) - 1


def encode_cell(x_index: int, y_index: int) -> str:
    return f"A{x_index + 1}B{y_index + 1}"


def cell_center_cm(x_index: int, y_index: int, height: int) -> PointCm:
    return PointCm(
        x_cm=FIELD_MARGIN_CM + ((x_index + 0.5) * CELL_SIZE_CM),
        y_cm=FIELD_MARGIN_CM + (((height - y_index) - 0.5) * CELL_SIZE_CM),
    )


def cell_code_center_cm(cell_code: str, height: int) -> PointCm:
    x_index, y_index = decode_cell(cell_code)
    return cell_center_cm(x_index, y_index, height)


def euclidean_distance_cm(from_point: PointCm, to_point: PointCm) -> float:
    return math.hypot(to_point.x_cm - from_point.x_cm, to_point.y_cm - from_point.y_cm)


def heading_degrees(from_point: PointCm, to_point: PointCm) -> float:
    return math.degrees(math.atan2(to_point.y_cm - from_point.y_cm, to_point.x_cm - from_point.x_cm))


def compute_descent_run_cm(cruise_height_cm: float, descent_angle_deg: float) -> float:
    return cruise_height_cm / math.tan(math.radians(descent_angle_deg))


def compute_descent_run_bounds_cm(
    cruise_height_cm: float,
    descent_angle_deg: float,
    angle_tolerance_deg: float = 5.0,
) -> tuple[float, float]:
    return (
        compute_descent_run_cm(cruise_height_cm, descent_angle_deg + angle_tolerance_deg),
        compute_descent_run_cm(cruise_height_cm, descent_angle_deg - angle_tolerance_deg),
    )


def _normalize_heading_difference_deg(delta_deg: float) -> float:
    normalized = (delta_deg + 180.0) % 360.0 - 180.0
    return abs(normalized)


def terminal_cells_for_landing(
    width: int,
    height: int,
    no_fly_cells: set[str],
    landing_profile: LandingProfile,
) -> set[str]:
    lower_run_cm, upper_run_cm = compute_descent_run_bounds_cm(
        landing_profile.cruise_height_cm,
        landing_profile.descent_angle_deg,
    )
    candidates: set[str] = set()
    for y_index in range(height):
        for x_index in range(width):
            cell_code = encode_cell(x_index, y_index)
            if cell_code in no_fly_cells:
                continue
            center = cell_center_cm(x_index, y_index, height)
            distance_cm = euclidean_distance_cm(center, landing_profile.takeoff_anchor_cm)
            if not (lower_run_cm <= distance_cm <= upper_run_cm):
                continue

            approach_heading_deg = heading_degrees(center, landing_profile.takeoff_anchor_cm)
            if _normalize_heading_difference_deg(
                approach_heading_deg - landing_profile.preferred_heading_deg
            ) > landing_profile.heading_tolerance_deg:
                continue
            candidates.add(cell_code)
    return candidates
