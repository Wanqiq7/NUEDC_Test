# Obstacle-Aware Route Optimization Implementation Plan

> **Superseded (2026-07-12):** The DFS-tree fallback, Legacy mission mode, closed-tour API, and heading/revisit scoring described below were removed. The active implementation is the landing-aware XY time model in `2026-07-12-unified-time-optimal-route.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shorten H-problem ground-station coverage routes while preserving mandatory coverage, adjacency, no-fly avoidance, and landing compatibility.

**Architecture:** Add a deterministic segment-based candidate planner in `h_problem_core`, select candidates using the mission cost ordering, and retain the existing DFS tree route as a fallback. The `MissionPlan` and ground-station/airborne protocol boundaries remain unchanged.

**Tech Stack:** C++17, Qt6 Core/Test, CMake

## Global Constraints

- Plan all 60 flyable cells in a 9 x 7 grid with one three-cell forbidden region.
- Keep all consecutive route cells orthogonally adjacent.
- Do not change generated Protobuf files or TaskPlan wire format.
- Use a bounded deterministic algorithm and preserve a usable fallback route.

---

### Task 1: Lock the current detour as a failing regression

**Files:**
- Modify: `shared/cpp/tests/test_h_route_planner.cpp`

**Interfaces:**
- Consumes: `hcore::planRoute(...)`, `hcore::terminalCellsForLanding(...)`
- Produces: regression coverage for the `A9B1` sample mission.

- [ ] **Step 1: Write a default-mission regression test**

Add a `timeOptimalSampleRouteImprovesOn68WaypointBaseline()` Qt test using start `A9B1`, forbidden cells `A4B3/A5B3/A6B3`, and the sample landing profile. Assert a non-empty adjacent route, 60 unique cells, a landing-compatible final cell, and `route.size() < 68`.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build build --target test_h_route_planner && ctest --test-dir build --output-on-failure -R test_h_route_planner`

Expected: the new baseline assertion fails because the current route has 68 waypoints.

### Task 2: Add deterministic segment-route candidates

**Files:**
- Modify: `shared/cpp/src/planning/route_planner.cpp`
- Modify: `shared/cpp/tests/test_h_route_planner.cpp`

**Interfaces:**
- Produces: internal route candidates that start at the requested cell, cover every flyable cell, and terminate in a supplied landing cell.
- Consumes: `gridNeighbors`, `shortestPath`, `estimateRouteCost`.

- [ ] **Step 1: Introduce internal segment and candidate helpers**

Represent each maximal row or column run of flyable cells by its ordered endpoints. Generate both orientations, stitch compatible segment candidates with adjacent or shortest valid connector paths, and reject candidates that fail full coverage or contain invalid moves.

- [ ] **Step 2: Select candidates deterministically**

Compare candidates by move count, heading changes, repeated visits, landing distance, then lexicographic route order. Limit construction to the 9 x 7 map and retain no recursion without a time bound.

- [ ] **Step 3: Run the focused test and verify GREEN**

Run: `cmake --build build --target test_h_route_planner && ctest --test-dir build --output-on-failure -R test_h_route_planner`

Expected: default mission route is valid and shorter than 68 waypoints.

### Task 3: Preserve coverage and fallback behavior

**Files:**
- Modify: `shared/cpp/src/planning/route_planner.cpp`
- Modify: `shared/cpp/tests/test_h_route_planner.cpp`
- Modify: `shared/cpp/tests/test_h_planning_result.cpp`

**Interfaces:**
- Produces: `PlanningResult` route/cost/coverage fields consistent with the existing public API.

- [ ] **Step 1: Add horizontal and vertical no-fly coverage cases**

Test representative horizontal and vertical three-cell barriers from `A9B1`. Assert no forbidden cells, 60 unique covered cells, adjacency, and a terminal in `terminalCellsForLanding`.

- [ ] **Step 2: Retain legacy fallback**

Use `planLegacyRoute` only when no segment candidate is valid; preserve its current failure message contract.

- [ ] **Step 3: Run focused planning tests**

Run: `cmake --build build --target test_h_route_planner test_h_planning_result && ctest --test-dir build --output-on-failure -R 'test_h_route_planner|test_h_planning_result'`

Expected: all focused planner tests pass.

### Task 4: Verify the ground-station integration boundary

**Files:**
- No production changes expected.

**Interfaces:**
- Consumes: `hcore::buildMissionPlan(...)` and `MissionPlanBridge::generatePlan(...)`.

- [ ] **Step 1: Run mission bridge and controller tests**

Run: `cmake --build build --target test_mission_plan_bridge test_h_mission_controller && ctest --test-dir build --output-on-failure -R 'test_mission_plan_bridge|test_h_mission_controller'`

Expected: generated shorter route remains serializable and is still accepted by the ground-station mission workflow.

- [ ] **Step 2: Run the complete C++ test suite**

Run: `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`

Expected: all tests pass.
