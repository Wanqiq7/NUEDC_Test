# Ground-Airborne Communication Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make task communication fail closed, isolate messages by task, derive UI state from authoritative ACK/Summary messages, and expose a tested flight-executor boundary instead of returning false START success.

**Architecture:** Ground state filtering remains in `HMissionController`, while physical telemetry health remains in `MainWindow`. Shared command handlers enforce task ownership and mission lifecycle rules. The ROS bridge delegates actual movement to a `FlightExecutor`; production uses no executor until a real STM32 adapter exists, so START is rejected instead of faking execution.

**Tech Stack:** C++17, Qt6/Qt Test, Protobuf, ZeroMQ, ROS 2 Humble, ament/colcon, GTest.

## Global Constraints

- Keep `shared/proto/messages.proto` and ports `5557/5558` unchanged.
- Do not invent or extend the STM32 serial frame without its firmware contract.
- Only START/STOP ACK and current-task Summary may change ground mission running state.
- START must fail without a configured real flight executor.
- Preserve the existing XY translation model; do not add yaw commands or yaw cost.
- Add a minimal failing test before each behavior change.

---

### Task 1: Ground Task Isolation And Authoritative Running State

**Files:**
- Modify: `ground_station_computer/tests/test_h_mission_controller.cpp`
- Modify: `ground_station_computer/src/h_problem/mission/h_mission_controller.cpp`

**Interfaces:**
- Consumes: `HMissionController::handleTaskEvent`, `handleTaskSummary`, `applyCommandAck`.
- Produces: task-filtered event handling where telemetry never calls `AirborneSyncState::setRunning(true)`.

- [ ] **Step 1: Add failing controller tests**

Add test slots covering these exact assertions:

```cpp
void telemetryDoesNotStartMission();
void telemetryAfterStopDoesNotRestartMission();
void ignoresEventsFromAnotherTask();
void ignoresSummaryFromAnotherTask();
void ignoresAckFromAnotherTask();
```

Construct events with `task_id = "other-task"`; verify current cell, detection totals, target text, running state and summary totals remain unchanged. For current-task telemetry verify the cell changes while `missionRunning()` remains false.

- [ ] **Step 2: Run the focused test and verify failure**

Run:

```bash
cmake --build build --target test_h_mission_controller -j4
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_h_mission_controller
```

Expected: the new telemetry and task-isolation cases fail against current behavior.

- [ ] **Step 3: Implement task filtering and remove telemetry state transition**

Add one local guard used before decoding:

```cpp
bool HMissionController::isCurrentTaskMessage(const QString &task_id) const {
    return !task_id.isEmpty() && task_id == current_case_id_;
}
```

Reject mismatched TaskEvent, TaskSummary and non-empty ACK task IDs with `qDebug()`. Require `summary.task_type == "h_problem"`. In `applyTelemetry`, update text/cell only and do not mutate `sync_state_.running()`.

- [ ] **Step 4: Run the controller suite**

Run the command from Step 2. Expected: all controller tests pass.

- [ ] **Step 5: Commit the ground controller change**

```bash
git add ground_station_computer/src/h_problem/mission/h_mission_controller.cpp \
        ground_station_computer/src/h_problem/mission/h_mission_controller.h \
        ground_station_computer/tests/test_h_mission_controller.cpp
git commit -m "fix: isolate ground mission messages"
```

### Task 2: Ground Telemetry Health TTL

**Files:**
- Modify: `ground_station_computer/src/app/main_window.h`
- Modify: `ground_station_computer/src/app/main_window.cpp`
- Modify: `ground_station_computer/tests/test_main_window.cpp`

**Interfaces:**
- Produces: `recordTelemetryReceived()` and `telemetryLinkHealthy(qint64 now_ms = QDateTime::currentMSecsSinceEpoch()) const`.

- [ ] **Step 1: Add a failing TTL test**

Add `MainWindow::telemetryLinkHealthyAt(qint64 now_ms) const` and `telemetryStatusTextAt(qint64 now_ms) const` as private helpers, with `MainWindowTests` declared as a friend. Assert: no message yields `遥测: 等待`; a fresh message yields `遥测: 已接收`; a receive time older than 5000 ms yields `遥测: 超时`.

- [ ] **Step 2: Build and run MainWindow tests**

```bash
cmake --build build --target test_main_window -j4
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_main_window
```

Expected: the new expiry assertion fails because `telemetry_online_` never resets.

- [ ] **Step 3: Replace the permanent boolean with a timestamp**

Use:

```cpp
static constexpr qint64 kTelemetryLinkTtlMs = 5000;
qint64 last_successful_telemetry_ms_ = 0;
```

Every accepted subscriber message calls `recordTelemetryReceived()`. `refreshAirborneStatusLabel()` renders `等待`, `已接收`, or `超时` from the timestamp. Reuse the existing one-second UI health timer.

- [ ] **Step 4: Run MainWindow tests**

Run Step 2. Expected: all MainWindow tests pass.

- [ ] **Step 5: Commit telemetry health**

```bash
git add ground_station_computer/src/app/main_window.cpp \
        ground_station_computer/src/app/main_window.h \
        ground_station_computer/tests/test_main_window.cpp
git commit -m "fix: expire stale telemetry health"
```

### Task 3: Shared Command Lifecycle And Task Ownership

**Files:**
- Modify: `shared/cpp/include/competition_core/task/models.h`
- Modify: `shared/cpp/src/protocol/competition_command_handler.cpp`
- Modify: `shared/cpp/tests/test_task_protocol.cpp`
- Mirror semantically into: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_competition_core/include/competition_core/task/models.h`
- Mirror semantically into: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_competition_core/src/protocol/competition_command_handler.cpp`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_competition_core/tests/test_task_protocol.cpp`

**Interfaces:**
- Produces: `CommandState::replaceMission(const TaskPlan&)`, `completeMission()`, and task ownership validation for non-PING control commands.

- [ ] **Step 1: Add failing lifecycle tests in both repositories**

Cover:

```cpp
startWithoutLoadedMissionIsRejected();
controlForAnotherTaskIsRejected();
missionReplacementResetsRunningStopAndVision();
pingDoesNotRequireMatchingTaskId();
```

Verify rejected commands do not advance `last_accepted_sequence`.

- [ ] **Step 2: Run both protocol suites and verify failure**

```bash
cmake --build build --target test_task_protocol -j4
QT_QPA_PLATFORM=offscreen ./build/shared/cpp/test_task_protocol
```

and:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select nuedc_competition_core
colcon test --packages-select nuedc_competition_core
```

- [ ] **Step 3: Implement lifecycle helpers and guards**

`replaceMission` atomically installs the plan and resets start, stop and vision before marking loaded. START checks loaded and task ID; STOP/ARM/RESET check task ID. PING remains a state query. Failure ACKs are enriched from current state by the server-level stateful ACK builder.

- [ ] **Step 4: Run both protocol suites**

Expected: both copies pass the same ownership and lifecycle cases.

- [ ] **Step 5: Commit the ground shared-core copy**

```bash
git add shared/cpp/include/competition_core/task/models.h \
        shared/cpp/src/protocol/competition_command_handler.cpp \
        shared/cpp/tests/test_task_protocol.cpp
git commit -m "fix: enforce command task ownership"
```

Do not commit the airborne workspace unless its existing dirty changes have been reviewed and intentionally included.

### Task 4: Testable Flight Executor Boundary

**Files:**
- Create: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/include/nuedc_bridge/flight_executor.h`
- Create: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/include/nuedc_bridge/mission_execution_state.h`
- Create: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/src/mission_execution_state.cpp`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/CMakeLists.txt`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/test/test_bridge_conversions.cpp`

**Interfaces:**
- Produces: `FlightExecutor::isAvailable()`, `startMission(const competition::TaskPlan&, QString*)`, `stopMission()` and `MissionExecutionState` load/start/stop/record/complete transitions.

- [ ] **Step 1: Add failing GTests with a fake executor**

Test no executor, unavailable executor, executor rejection, successful start, stop cancellation, wrong task ID, mission replacement reset, unique visited-cell counting, animal total accumulation, and exactly-once completion Summary creation.

- [ ] **Step 2: Build bridge tests and verify failure**

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select nuedc_bridge
colcon test --packages-select nuedc_bridge
```

- [ ] **Step 3: Implement the minimal interface and pure state object**

The production bridge passes `nullptr` until a real adapter is configured. `start()` returns `flight executor is not configured` without setting running or accepting the command. `recordVisitedCell(QString)` stores a set, `recordDetection(QString animal_name, int count)` accumulates totals, and `complete()` returns a `competition::TaskSummary` once with `visited_waypoints` plus compact JSON totals, then clears running and vision.

- [ ] **Step 4: Run bridge tests**

Expected: all bridge tests pass, including fail-closed START behavior.

### Task 5: Integrate Bridge Command, Telemetry And Summary Flow

**Files:**
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/src/nuedc_bridge_node.cpp`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/src/bridge_conversions.cpp`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/include/nuedc_bridge/bridge_conversions.h`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/nuedc_bridge/test/test_bridge_conversions.cpp`

**Interfaces:**
- Consumes: `MissionExecutionState` from Task 4.
- Produces: stateful command ACKs, running-only task telemetry, and serialized TaskSummary envelopes.

- [ ] **Step 1: Add bridge conversion and flow tests**

Add a Summary serialization test and pure helper tests proving telemetry is suppressed when not running and task counters reset on MissionLoad.

- [ ] **Step 2: Run bridge tests and verify failure**

Run the Task 4 test commands.

- [ ] **Step 3: Replace bridge booleans with the state object**

Always update cached odometry. Publish task telemetry only when current mission is running and record its mapped cell. Validate task ID for START/STOP/ARM/RESET. Route START through `FlightExecutor`; with current production `nullptr`, return failure. Accepted detection events update mission totals. Wire `publishSummary` to the executor completion callback and clear vision/running before publishing.

- [ ] **Step 4: Run bridge tests**

Expected: all bridge tests pass.

### Task 6: Cross-Repository Verification

**Files:**
- Verify only; no source edits unless a failing test identifies a regression.

- [ ] **Step 1: Build and test the ground repository**

```bash
cd /home/sb/Ground_station/NUEDC_Test
cmake -S . -B build
cmake --build build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Run ZeroMQ loopback tests outside the restricted sandbox if `ip_resolver Operation not permitted` appears.

- [ ] **Step 2: Build and test the three airborne packages**

```bash
cd /home/sb/Ground_station/point_lio_mid360_ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select nuedc_competition_core airborne_runtime nuedc_bridge
colcon test --packages-select nuedc_competition_core airborne_runtime nuedc_bridge
```

Expected: all selected packages pass.

- [ ] **Step 3: Verify protocol identity and working trees**

```bash
sha256sum /home/sb/Ground_station/NUEDC_Test/shared/proto/messages.proto \
          /home/sb/Ground_station/point_lio_mid360_ros2/shared/proto/messages.proto
git -C /home/sb/Ground_station/NUEDC_Test status --short
git -C /home/sb/Ground_station/point_lio_mid360_ros2 status --short
```

Expected: proto hashes match; generated build output is not staged; pre-existing airborne edits remain preserved.
