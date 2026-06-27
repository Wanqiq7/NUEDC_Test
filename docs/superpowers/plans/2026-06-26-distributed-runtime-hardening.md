# Distributed Runtime Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复机载端/地面站分布式运行链路中的线程安全、命令幂等性和图形动态更新性能风险。

**Architecture:** 保持现有 `competition_core` 通用协议、机载 `CommandServer`、地面站 Adapter/Shell 边界不变。新增线程安全状态封装，给控制命令和任务加载增加单调序列/版本处理，并将动态图形 marker 改为复用式更新。

**Tech Stack:** C++17, Qt6, Protobuf, ZeroMQ, Qt Test.

---

### Task 1: Thread-Safe Command State

**Files:**
- Modify: `shared/cpp/include/competition_core/task/models.h`
- Modify: `shared/cpp/src/protocol/competition_command_handler.cpp`
- Modify: `airborne_computer/src/main.cpp`
- Modify: `airborne_computer/src/airborne_runtime.cpp`
- Modify: `shared/cpp/tests/test_h_command_handler.cpp`
- Modify: `airborne_computer/tests/test_airborne_runtime.cpp`

- [ ] **Step 1: Add a failing test for concurrent state access**

Add a Qt Test case that repeatedly starts/stops through `competition::handleCommandBytes()` while another loop reads state via the new intended API.

- [ ] **Step 2: Introduce synchronized state API**

Add a small `CommandState` API using atomics for `start_requested`, `stop_requested`, `mission_loaded`, and mutex-protected accessors for `active_task_plan`.

- [ ] **Step 3: Replace direct field reads/writes**

Update command handler, `main.cpp`, and mission runtime to use `requestStart()`, `requestStop()`, `startRequested()`, `stopRequested()`, `markMissionLoaded()`, and `activeTaskPlan()`.

- [ ] **Step 4: Run focused tests**

Run `ctest --test-dir build-msys-ucrt64 -R "test_h_command_handler|test_airborne_runtime" --output-on-failure`.

### Task 2: Command Sequence And State Alignment

**Files:**
- Modify: `shared/proto/messages.proto`
- Modify: `shared/cpp/include/competition_core/task/models.h`
- Modify: `shared/cpp/src/protocol/envelope_codec.cpp`
- Modify: `shared/cpp/src/protocol/competition_command_handler.cpp`
- Modify: `ground_station_computer/src/framework/communication/zmq_command_client.cpp`
- Modify: `ground_station_computer/src/framework/communication/zmq_command_client.h`
- Modify: `ground_station_computer/src/framework/communication/reliable_command_client.cpp`
- Modify: `ground_station_computer/tests/test_zmq_command_client.cpp`
- Modify: `shared/cpp/tests/test_task_protocol.cpp`
- Modify: `shared/cpp/tests/test_h_command_handler.cpp`

- [ ] **Step 1: Add failing tests for non-zero command sequence and stale command rejection**

Assert that ground station control envelopes receive increasing sequence numbers and that command handler rejects stale `control_command` or `mission_load` envelopes.

- [ ] **Step 2: Extend Ack with runtime state**

Add optional fields to `Ack`: `task_id`, `mission_loaded`, `mission_running`, `last_accepted_sequence`.

- [ ] **Step 3: Track accepted sequences in `CommandState`**

Reject command envelopes with sequence less than or equal to the last accepted command sequence, except ping.

- [ ] **Step 4: Parse Ack state on ground station**

Extend `CommandSendResult` to carry returned state fields while preserving existing success/message behavior.

- [ ] **Step 5: Run protocol and command tests**

Run `ctest --test-dir build-msys-ucrt64 -R "test_task_protocol|test_h_command_handler|test_zmq_command_client|test_reliable_command_client|test_mission_command_service" --output-on-failure`.

### Task 3: Reuse Dynamic Graphics Items

**Files:**
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.cpp`
- Modify: `ground_station_computer/tests/test_grid_scene.cpp`

- [ ] **Step 1: Add failing test for marker reuse**

Add a test that calls `setCurrentCell()` repeatedly and asserts the scene item count does not grow and the same marker item is reused.

- [ ] **Step 2: Reuse current marker item**

Create `current_marker_` once, move it with `setRect()` or `setPos()` on subsequent telemetry updates, and hide/remove only when the cell is empty.

- [ ] **Step 3: Keep static grid and route behavior unchanged**

Do not change route drawing semantics; only optimize current position marker lifecycle.

- [ ] **Step 4: Run focused tests**

Run `ctest --test-dir build-msys-ucrt64 -R "test_grid_scene|test_main_window" --output-on-failure`.

### Task 4: Final Integration Review

**Files:**
- Review all modified files from Tasks 1-3.

- [ ] **Step 1: Run full relevant test suite**

Run `ctest --test-dir build-msys-ucrt64 --output-on-failure`.

- [ ] **Step 2: Review for architecture boundary regressions**

Confirm H problem logic remains outside `competition_core`, UI remains outside shared core, and ZeroMQ sockets remain thread-local.

- [ ] **Step 3: Summarize residual risk**

Document any remaining production hardening gaps such as heartbeat cadence or persistent command sequence storage.
