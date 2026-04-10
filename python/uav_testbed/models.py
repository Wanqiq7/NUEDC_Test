from __future__ import annotations

from dataclasses import dataclass

from .mission_geometry import LandingProfile, PointCm


@dataclass(frozen=True)
class Animal:
    cell: str
    name: str
    count: int


@dataclass(frozen=True)
class LandingConfig:
    takeoff_anchor_cm: PointCm
    descent_angle_deg: float
    cruise_height_cm: float
    touchdown_radius_cm: float
    preferred_heading_deg: float = 45.0

    def to_profile(self) -> LandingProfile:
        return LandingProfile(
            takeoff_anchor_cm=self.takeoff_anchor_cm,
            cruise_height_cm=self.cruise_height_cm,
            descent_angle_deg=self.descent_angle_deg,
            touchdown_radius_cm=self.touchdown_radius_cm,
            preferred_heading_deg=self.preferred_heading_deg,
        )


@dataclass(frozen=True)
class CaseConfig:
    case_id: str
    start_cell: str
    no_fly_cells: tuple[str, ...]
    tick_interval_ms: int
    animals: tuple[Animal, ...]
    return_to_start: bool = False
    landing: LandingConfig | None = None
