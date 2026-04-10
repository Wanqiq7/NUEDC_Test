# Time-Optimal Open Coverage With Landing Corridor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current closed-tour / repeated-cell optimization with a mission planner that minimizes estimated real flight time, covers every flyable grid cell, and finishes inside a landing-compatible terminal region that can hand off to the required `45° ± 5°` descent into the red takeoff zone.

**Architecture:** Model the mission as four linked stages: takeoff-zone to entry, open coverage over the `9 x 7` grid, terminal-region selection near the lower-right side of the field, and descent-corridor handoff. Keep ZeroMQ, Protobuf, Qt6, and the simulator intact, but replace the route objective and endpoint model inside the Python planner. Use a landing-aware open-route optimizer first, with the existing continuous grid planner only as a fallback for unreachable edge cases.

**Tech Stack:** Python 3.10, `unittest`, Qt6 Widgets, ZeroMQ, Protobuf, JSON case configs

---

### Task 1: Lock the corrected problem interpretation with tests and config schema

**Files:**
- Modify: `python/tests/test_route_planner.py`
- Modify: `python/tests/test_simulator.py`
- Modify: `python/uav_testbed/models.py`
- Modify: `python/uav_testbed/simulator.py`
- Modify: `cases/sample_case.json`

- [ ] **Step 1: Add mission-level config fields for takeoff zone, terminal corridor, and descent geometry**

```python
@dataclass(frozen=True)
class LandingConfig:
    takeoff_anchor_cm: tuple[float, float]
    descent_angle_deg: float
    cruise_height_cm: float
    touchdown_radius_cm: float
    preferred_heading_deg: float


@dataclass(frozen=True)
class CaseConfig:
    case_id: str
    start_cell: str | None
    no_fly_cells: tuple[str, ...]
    tick_interval_ms: int
    animals: tuple[Animal, ...]
    return_to_start: bool = False
    landing: LandingConfig | None = None
```

- [ ] **Step 2: Replace the old closed-tour expectation with landing-aware endpoint expectations**

```python
def test_plan_route_prefers_landing_compatible_terminal_region() -> None:
    route = plan_route(
        width=9,
        height=7,
        start_cell="A9B1",
        no_fly_cells={"A4B3", "A5B3", "A6B3"},
        mission_mode="time_optimal_open",
        landing_profile=LandingProfile(
            takeoff_anchor_cm=(475.0, 375.0),
            cruise_height_cm=120.0,
            descent_angle_deg=45.0,
            touchdown_radius_cm=18.0,
        ),
    )

    assert route[0] in {"A9B1", "A8B1", "A9B2"}
    assert route[-1] in {"A9B1", "A8B1", "A9B2", "A8B2", "A7B1"}
    assert len(set(route)) == 60
```

- [ ] **Step 3: Add a regression test for real mission cost instead of route-vertex count**

```python
def test_time_optimal_open_route_beats_closed_cycle_cost() -> None:
    closed_route = plan_route(
        width=9,
        height=7,
        start_cell="A9B7",
        no_fly_cells={"A4B3", "A5B3", "A6B3"},
        end_cell="A9B7",
        require_cycle=True,
    )
    open_route = plan_route(
        width=9,
        height=7,
        start_cell="A9B1",
        no_fly_cells={"A4B3", "A5B3", "A6B3"},
        mission_mode="time_optimal_open",
        landing_profile=DEFAULT_SAMPLE_LANDING_PROFILE,
    )

    assert estimate_route_cost(open_route) < estimate_route_cost(closed_route)
```

- [ ] **Step 4: Run Python tests and confirm the new expectations fail before implementation**

Run: `python3 -m unittest discover -s python/tests -v`
Expected: FAIL in the new landing/mission-cost tests because `LandingProfile`, `mission_mode`, and `estimate_route_cost` do not exist yet.

- [ ] **Step 5: Commit the red-state test/schema changes**

```bash
git add python/tests/test_route_planner.py python/tests/test_simulator.py python/uav_testbed/models.py python/uav_testbed/simulator.py cases/sample_case.json
git commit -m "test: lock landing-aware mission planning requirements"
```

### Task 2: Introduce geometric mission primitives for takeoff, terminal region, and descent

**Files:**
- Create: `python/uav_testbed/mission_geometry.py`
- Modify: `python/uav_testbed/models.py`
- Test: `python/tests/test_route_planner.py`

- [ ] **Step 1: Define reusable geometry types and conversion helpers**

```python
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
    preferred_heading_deg: float = -45.0
```

- [ ] **Step 2: Implement the descent-run calculator and terminal-distance bounds**

```python
def compute_descent_run_cm(cruise_height_cm: float, descent_angle_deg: float) -> float:
    radians_value = math.radians(descent_angle_deg)
    return cruise_height_cm / math.tan(radians_value)


def compute_descent_run_bounds_cm(cruise_height_cm: float) -> tuple[float, float]:
    return (
        compute_descent_run_cm(cruise_height_cm, 50.0),
        compute_descent_run_cm(cruise_height_cm, 40.0),
    )
```

- [ ] **Step 3: Add a helper that maps field geometry to acceptable terminal cells**

```python
def terminal_cells_for_landing(
    width: int,
    height: int,
    no_fly_cells: set[str],
    landing_profile: LandingProfile,
) -> set[str]:
    lower_run_cm, upper_run_cm = compute_descent_run_bounds_cm(landing_profile.cruise_height_cm)
    candidates: set[str] = set()
    for y_index in range(height):
        for x_index in range(width):
            cell = encode_cell(x_index, y_index)
            if cell in no_fly_cells:
                continue
            center = cell_center_cm(x_index, y_index)
            distance = euclidean_distance_cm(center, landing_profile.takeoff_anchor_cm)
            if lower_run_cm <= distance <= upper_run_cm:
                candidates.add(cell)
    return candidates
```

- [ ] **Step 4: Run the focused test selection to verify geometry helpers pass**

Run: `python3 -m unittest python.tests.test_route_planner.RoutePlannerTests.test_exact_completion_to_end_finds_shortest_tail_on_small_grid -v`
Expected: PASS, and any new geometry test added in Task 1 passes after implementation.

- [ ] **Step 5: Commit the mission-geometry layer**

```bash
git add python/uav_testbed/mission_geometry.py python/uav_testbed/models.py python/tests/test_route_planner.py
git commit -m "feat: add landing geometry primitives"
```

### Task 3: Replace the route objective with estimated flight cost

**Files:**
- Create: `python/uav_testbed/route_cost.py`
- Modify: `python/uav_testbed/route_planner.py`
- Test: `python/tests/test_route_planner.py`

- [ ] **Step 1: Add a cost function that reflects actual mission time drivers**

```python
def estimate_route_cost(
    route: list[str],
    turn_penalty_cm: float = 18.0,
    repeated_cell_penalty_cm: float = 6.0,
) -> float:
    distance_cost = 0.0
    repeated_visits: dict[str, int] = {}
    for previous, current in zip(route, route[1:]):
        distance_cost += euclidean_cell_distance_cm(previous, current)
        repeated_visits[current] = repeated_visits.get(current, 0) + 1

    turn_cost = count_heading_changes(route) * turn_penalty_cm
    repeat_cost = sum(max(visit_count - 1, 0) for visit_count in repeated_visits.values()) * repeated_cell_penalty_cm
    return distance_cost + turn_cost + repeat_cost
```

- [ ] **Step 2: Stop using `require_cycle=True` as the optimization target for the main planner**

```python
def plan_route(..., mission_mode: str = "time_optimal_open", landing_profile: LandingProfile | None = None) -> list[str]:
    if mission_mode == "time_optimal_open":
        return _plan_time_optimal_open_route(...)
    if require_cycle:
        return _plan_legacy_closed_cycle(...)
    return _plan_legacy_open_route(...)
```

- [ ] **Step 3: Add the failing regression for cost comparison and implement the minimum viable estimator**

```python
def test_estimate_route_cost_penalizes_detours_and_turns() -> None:
    direct_route = ["A1B1", "A2B1", "A3B1"]
    detour_route = ["A1B1", "A1B2", "A2B2", "A2B1", "A3B1"]
    assert estimate_route_cost(direct_route) < estimate_route_cost(detour_route)
```

- [ ] **Step 4: Run all Python tests and confirm route-cost behavior is green**

Run: `python3 -m unittest discover -s python/tests -v`
Expected: PASS with the new `estimate_route_cost` assertions.

- [ ] **Step 5: Commit the new objective function**

```bash
git add python/uav_testbed/route_cost.py python/uav_testbed/route_planner.py python/tests/test_route_planner.py
git commit -m "feat: switch planner objective to flight cost"
```

### Task 4: Build an open coverage planner with landing-compatible terminal selection

**Files:**
- Modify: `python/uav_testbed/route_planner.py`
- Create: `python/uav_testbed/coverage_segments.py`
- Test: `python/tests/test_route_planner.py`

- [ ] **Step 1: Represent the grid as continuous coverage segments instead of forcing cell-by-cell cycle closure**

```python
@dataclass(frozen=True)
class CoverageSegment:
    row_index: int
    start_cell: str
    end_cell: str
    covered_cells: tuple[str, ...]


def build_row_segments(width: int, height: int, no_fly_cells: set[str]) -> list[CoverageSegment]:
    segments: list[CoverageSegment] = []
    for y_index in range(height):
        current_cells: list[str] = []
        for x_index in range(width):
            cell = encode_cell(x_index, y_index)
            if cell in no_fly_cells:
                if current_cells:
                    segments.append(make_segment(y_index, current_cells))
                    current_cells = []
                continue
            current_cells.append(cell)
        if current_cells:
            segments.append(make_segment(y_index, current_cells))
    return segments
```

- [ ] **Step 2: Enumerate legal sweep orders and choose the minimum-cost open path**

```python
def _plan_time_optimal_open_route(...) -> list[str]:
    terminal_cells = terminal_cells_for_landing(...)
    candidate_routes: list[list[str]] = []
    for sweep_family in ("row", "column"):
        segments = build_segments_for_family(sweep_family, ...)
        for orientation in ("forward", "reverse"):
            route = stitch_segments_open(segments, orientation, ...)
            if route[-1] in terminal_cells:
                candidate_routes.append(route)
            else:
                adjusted_route = retarget_route_suffix(route, terminal_cells, ...)
                candidate_routes.append(adjusted_route)
    return min(candidate_routes, key=estimate_route_cost)
```

- [ ] **Step 3: Add a regression that the chosen terminal is near the lower-right landing corridor**

```python
def test_time_optimal_open_route_ends_near_landing_corridor() -> None:
    route = plan_route(...)
    assert route[-1] in terminal_cells_for_landing(
        width=9,
        height=7,
        no_fly_cells={"A4B3", "A5B3", "A6B3"},
        landing_profile=DEFAULT_SAMPLE_LANDING_PROFILE,
    )
```

- [ ] **Step 4: Run the landing-aware route tests and confirm the planner no longer returns the old `A9B7` closed loop**

Run: `python3 -m unittest python.tests.test_route_planner -v`
Expected: PASS, and the open planner output ends near the lower-right terminal corridor instead of `A9B7`.

- [ ] **Step 5: Commit the open coverage planner**

```bash
git add python/uav_testbed/route_planner.py python/uav_testbed/coverage_segments.py python/tests/test_route_planner.py
git commit -m "feat: add landing-aware open coverage planner"
```

### Task 5: Integrate terminal-region and descent information into the simulator and ground station

**Files:**
- Modify: `python/uav_testbed/simulator.py`
- Modify: `ground_station/src/grid_scene.cpp`
- Modify: `ground_station/src/grid_scene.h`
- Modify: `ground_station/src/main_window.cpp`
- Modify: `ground_station/src/main_window.h`

- [ ] **Step 1: Extend simulator config messages with terminal and descent metadata**

```python
yield {
    "message_type": "config",
    "case_id": case.case_id,
    "start_cell": route[0],
    "route": route,
    "terminal_cell": route[-1],
    "landing_enabled": case.landing is not None,
    "descent_angle_deg": case.landing.descent_angle_deg if case.landing else None,
}
```

- [ ] **Step 2: Add a visual marker for the terminal cell and a dashed line for the descent corridor**

```cpp
void GridScene::setLandingTerminal(const QString &cell_code);
void GridScene::setDescentCorridor(const QString &from_cell, const QPointF &touchdown_point);
```

- [ ] **Step 3: Update the main window summary panel to show mission objective outputs**

```cpp
summary_label_->setText(
    QString("终点: %1 | 目标: 最短飞行时间 | 下降角: %2°")
        .arg(terminal_cell)
        .arg(descent_angle_deg, 0, 'f', 1));
```

- [ ] **Step 4: Rebuild the project and run `ctest`**

Run: `cmake -S . -B build && cmake --build build`
Expected: build succeeds

Run: `ctest --test-dir build --output-on-failure`
Expected: all Qt/C++ tests pass

- [ ] **Step 5: Commit the simulator/UI integration**

```bash
git add python/uav_testbed/simulator.py ground_station/src/grid_scene.cpp ground_station/src/grid_scene.h ground_station/src/main_window.cpp ground_station/src/main_window.h
git commit -m "feat: expose landing terminal and descent corridor in UI"
```

### Task 6: Validate mission behavior against the corrected contest interpretation

**Files:**
- Modify: `cases/sample_case.json`
- Create: `docs/superpowers/specs/2026-04-09-time-optimal-open-coverage-notes.md`

- [ ] **Step 1: Update the sample case to stop pretending the start/finish cell is `A9B7`**

```json
{
  "case_id": "wildlife-demo",
  "start_cell": "A9B1",
  "return_to_start": false,
  "landing": {
    "takeoff_anchor_cm": [475.0, 375.0],
    "cruise_height_cm": 120.0,
    "descent_angle_deg": 45.0,
    "touchdown_radius_cm": 18.0,
    "preferred_heading_deg": -45.0
  }
}
```

- [ ] **Step 2: Run the simulator end-to-end with the updated sample case**

Run: `PYTHONPATH=python python3 -m uav_testbed.run_simulator --case cases/sample_case.json --sleep-scale 0.02`
Expected: process exits cleanly and publishes `terminal_cell` / landing metadata.

- [ ] **Step 3: Run the ground station and manually inspect the route**

Run: `./build/ground_station/ground_station_app`
Expected: route starts near the lower-right entry area, ends in the landing-compatible terminal region, and displays descent metadata.

- [ ] **Step 4: Write validation notes tying the implementation back to the contest wording**

```markdown
- 题面“越快越好”已映射为总飞行成本最小化，不再以闭环或最少格数为主目标。
- 题面“完成巡查后以 45°±5° 俯角降落到起飞区域”已映射为终点候选区域和下降走廊约束。
- 题面红色起飞区位于右下角，规划器不再错误使用 `A9B7` 作为固定起终点。
```

- [ ] **Step 5: Commit the sample-case and validation-note updates**

```bash
git add cases/sample_case.json docs/superpowers/specs/2026-04-09-time-optimal-open-coverage-notes.md
git commit -m "docs: align mission model with contest landing rules"
```

## Self-Review

- Spec coverage: this plan covers corrected interpretation, mission geometry, objective function, open-route planning, UI/simulator integration, and validation against the PDF wording.
- Placeholder scan: no `TODO` / `TBD` placeholders remain; all tasks include files, commands, and expected outcomes.
- Type consistency: `LandingProfile`, `estimate_route_cost`, `terminal_cells_for_landing`, and `mission_mode="time_optimal_open"` are defined once and reused consistently across tasks.
