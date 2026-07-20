from dataclasses import dataclass


@dataclass(frozen=True)
class PlanningRequest:
    case_path: str
    no_fly_cells: list[str]
