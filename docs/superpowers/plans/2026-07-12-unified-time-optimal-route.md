# Unified Time-Optimal Route Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or executing-plans task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make H-problem planning use one XY-only, landing-aware, time-minimizing open-route implementation on both ground and airborne cores.

**Architecture:** The planner accepts a landing profile and timing model, produces only open coverage routes, and ranks routes solely by modeled mission time. A deterministic row/column sweep connector supplies a legal incumbent to the bounded search; it replaces the old DFS-tree/closed-tour fallback. Canonical `competition::TaskPlan` metadata is the only runtime plan interchange format.

**Tech Stack:** C++17, Qt6 Test, CMake, ROS 2 ament/colcon.

## Global Constraints

- Cover every non-forbidden grid cell with orthogonally adjacent moves.
- Do not traverse a no-fly cell, including anchor transit and descent corridor.
- Never include Yaw, turn count, or repeated-cell penalties in mission-time optimization.
- A 9 x 7 mission with a legal contiguous three-cell obstacle must either return a fully covered, landing-compatible open route or explicitly report that no landing-compatible terminal exists.
- Keep generated Protobuf and TaskPlan wire format unchanged.
- Preserve unknown/absent new metadata compatibility while rejecting deprecated `return_to_start: true` configuration.

---

### Task 1: Lock the no-legacy public contract

**Status:** Completed in the ground repository. The ROS duplicate core remains a separate synchronization task.

**Files:**
- Modify: `shared/cpp/tests/test_h_case_loader.cpp`
- Modify: `shared/cpp/tests/test_h_route_planner.cpp`
- Modify: `shared/cpp/tests/test_h_planning_result.cpp`
- Modify: `shared/cpp/include/h_problem_core/planning/route_planner.h`

**Interfaces:**
- Produces a `RouteRequest` with grid, start, no-fly cells, mandatory landing profile, and `MissionTiming`; no mode, end-cell, or closed-cycle option.
- Produces an explicit case-loader error for `return_to_start: true`.

- [ ] Add `rejectsDeprecatedReturnToStartConfiguration()` using a temporary JSON case containing `"return_to_start": true`; assert loading fails and the error mentions `return_to_start`.
- [ ] Run `cmake --build build --target test_h_case_loader && QT_QPA_PLATFORM=offscreen ./build/shared/cpp/test_h_case_loader rejectsDeprecatedReturnToStartConfiguration -v1`; expect failure because the current loader accepts the flag.
- [ ] Replace old legacy/closed-tour tests with TimeOptimal open-route coverage, exact-small-map, and explicit deprecated-config tests. Remove the legacy API declarations after all callers are migrated.
- [ ] Run `cmake --build build --target test_h_route_planner test_h_planning_result test_h_case_loader` and the three focused executables; expect all pass.

### Task 2: Replace the DFS-tree incumbent with deterministic sweep connectors

**Status:** Completed in the ground repository. The sweep seed and bounded search pass the default and all-layout regressions.

**Files:**
- Modify: `shared/cpp/src/planning/route_planner.cpp`
- Modify: `shared/cpp/src/planning/route_cost.cpp`
- Modify: `shared/cpp/include/h_problem_core/planning/route_cost.h`
- Modify: `shared/cpp/src/mission/mission_planning.cpp`

**Interfaces:**
- `buildSweepCoverageSeed(width, height, start_cell, no_fly_cells, terminal_cells, landing_profile, mission_timing)` returns the best legal sweep/shortest-connector route or an empty list.
- `planRoute(const RouteRequest&)` always performs TimeOptimal open planning and assigns `cost = estimated_mission_time_s`.

- [ ] Keep the 61-node default-route assertion and 93-layout matrix as failing guards while removing `planLegacyRoute`, Hamilton-cycle helpers, suffix repair, `MissionMode`, `end_cell`, and `require_cycle`.
- [ ] For each row/column snake orientation, connect ordered unblocked cells and a landing-compatible terminal with deterministic `shortestPath`; compare seeds using `estimateMissionTimeSeconds` only.
- [ ] Feed the best seed to `planBoundedCoverageRoute`; retain the exact <=16-cell oracle and truthful `SearchOptimality` status.
- [ ] Delete `estimateRouteCost`, heading-change/repeated-cell scoring, and their tests once no production call remains.
- [ ] Run `cmake --build build --target test_h_route_planner test_h_planning_result` and `QT_QPA_PLATFORM=offscreen ./build/shared/cpp/test_h_route_planner`; expect no Legacy symbols, all planner tests pass, and default route has 61 nodes.

### Task 3: Make canonical TaskPlan the only runtime H-plan format

**Files:**
- Modify: `shared/cpp/src/mission/case_loader.cpp`
- Modify: `shared/cases/sample_case.json`
- Modify: ground-station H plan storage/bridge tests and implementation only where a legacy H JSON format remains runtime source of truth.

**Interfaces:**
- `buildMissionPlan()` always requests the unified planner and populates landing approach, estimated time, status, and warnings.
- Ground storage uses core `taskPlanFromMissionPlan()` / `missionPlanFromTaskPlan()` rather than a separate H-plan JSON authority.

- [ ] Add an end-to-end store/load test with touchdown coordinates, descent data, estimate, optimality status, and warnings.
- [ ] Remove `return_to_start` from the case model and sample input; reject an explicit true value at the parser boundary.
- [ ] Run ground bridge/controller/store focused tests; expect canonical TaskPlan metadata to retain every field.

### Task 4: Mirror the finished core and remove airborne fallback paths

**Files:**
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_competition_core/{include/h_problem_core/common/models.h,src/mission/case_loader.cpp,src/mission/mission_planning.cpp,src/mission/mission_plan_store.cpp,src/planning/mission_geometry.cpp,src/planning/route_cost.cpp,src/planning/route_planner.cpp,src/protocol/envelope_builder.cpp}`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_runtime/src/airborne_runtime.cpp`
- Modify: corresponding duplicate-core and airborne-runtime tests.

**Interfaces:**
- The ROS core accepts the same unified `RouteRequest` and serializes the same optional MissionPlan metadata.
- `airborne::loadOptionalMissionPlan()` loads canonical TaskPlan only; no H JSON fallback controls runtime behavior.

- [ ] First add a TaskPlan -> `loadOptionalMissionPlan` test asserting touchdown, descent run/heading, mission time, optimality, and warnings survive; run it red against the old duplicate converter.
- [ ] Mirror ground core behavior after Task 2, retaining ROS-only `SimMessage` declarations in the model.
- [ ] Remove the runtime legacy JSON fallback and convert its regression to require a valid TaskPlan.
- [ ] Run `source /opt/ros/humble/setup.bash && colcon build --packages-select nuedc_competition_core airborne_runtime`, then `colcon test --packages-select nuedc_competition_core airborne_runtime` and `colcon test-result --verbose`; expect all selected tests pass.

### Task 5: Verify coverage, time, and integration

**Files:**
- No production edits expected.

- [ ] Run the default baseline test with `/usr/bin/time`; assert 61 nodes and record elapsed wall time.
- [ ] Run the 93 legal three-cell-layout matrix with independent landing-terminal enumeration; require full coverage and `< 300 s` modeled mission time for every feasible layout.
- [ ] Run `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure` outside the sandbox if local ZeroMQ loopback is required.
- [ ] Run `git diff --check` in both repositories and have a fresh reviewer inspect the final cross-repository diff.
