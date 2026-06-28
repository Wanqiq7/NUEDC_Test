# Canonical TaskPlan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `competition::TaskPlan` the canonical persisted and transmitted mission model while keeping H problem compatibility at a thin conversion seam.

**Architecture:** `competition_core` owns generic `TaskPlan` JSON, protobuf conversion, and validation. `h_problem_core` keeps H-specific planning types only as algorithm input/output and converts at the protocol seam with `taskPlanFromMissionPlan()` / `missionPlanFromTaskPlan()`. The H mission store becomes a compatibility adapter over `competition::TaskPlan` storage instead of a second independent JSON format.

**Tech Stack:** C++17, Qt6 Test, QJsonDocument/QSaveFile, Protobuf-generated `messages.pb.h`, existing CMake targets.

## Global Constraints

- Do not modify `ground_station_computer/src/app` or H UI/Adapter modules for this change.
- Do not edit generated files under `build/generated/proto/`.
- New comments and docs use Chinese when needed; code identifiers remain English.
- Use TDD: each task starts by writing or changing a failing Qt Test, then making the minimal implementation pass.
- Preserve compatibility for existing callers of `hcore::storeMissionPlan()` and `hcore::loadMissionPlan()` during this migration.
- Runtime persisted mission files should use canonical `competition::TaskPlan` JSON shape with `message_type: "task_plan"`.

---

### Task 1: Prove H mission storage uses canonical TaskPlan JSON

**Files:**
- Modify: `shared/cpp/tests/test_h_mission_plan_store.cpp`
- Modify: `shared/cpp/src/mission/mission_plan_store.cpp`

**Interfaces:**
- Consumes: `hcore::storeMissionPlan(const MissionPlan &, const QString &, QString *)`
- Consumes: `hcore::loadMissionPlan(const QString &, QString *)`
- Produces: compatibility behavior where H store writes canonical `TaskPlan` JSON and still reads it back as `hcore::MissionPlan`

- [ ] **Step 1: Write the failing test**

In `shared/cpp/tests/test_h_mission_plan_store.cpp`, change `storesReceivedMissionPlan()` so it expects canonical `TaskPlan` JSON:

```cpp
void HMissionPlanStoreTests::storesReceivedMissionPlan() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const QString output_path = QDir(dir.path()).filePath("runtime/active_mission_plan.json");
    QVERIFY2(hcore::storeMissionPlan(makePlan(), output_path, &error), qPrintable(error));

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("message_type").toString(), QString("task_plan"));
    QCOMPARE(object.value("task_id").toString(), QString("demo"));
    QCOMPARE(object.value("task_type").toString(), QString("h_problem"));
    QCOMPARE(object.value("start_waypoint_id").toString(), QString("A9B1"));
    QCOMPARE(object.value("terminal_waypoint_id").toString(), QString("A7B1"));
    QCOMPARE(object.value("waypoints").toArray().at(2).toObject().value("id").toString(), QString("A7B1"));

    const QJsonObject metadata = QJsonDocument::fromJson(object.value("metadata_json").toString().toUtf8()).object();
    QCOMPARE(metadata.value("case_id").toString(), QString("demo"));
    QCOMPARE(metadata.value("no_fly_cells").toArray().at(1).toString(), QString("A2B2"));
    QCOMPARE(metadata.value("landing_enabled").toBool(), true);
}
```

Add a new test slot declaration:

```cpp
void readsLegacyMissionPlanJsonDuringMigration();
```

Add the new compatibility test:

```cpp
void HMissionPlanStoreTests::readsLegacyMissionPlanJsonDuringMigration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = QDir(dir.path()).filePath("legacy_mission_plan.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QJsonDocument(hcore::missionPlanToJson(makePlan())).toJson(QJsonDocument::Compact));
    file.close();

    QString error;
    const auto loaded = hcore::loadMissionPlan(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->case_id, QString("demo"));
    QCOMPARE(loaded->route, QStringList({"A9B1", "A8B1", "A7B1"}));
    QCOMPARE(loaded->landing_enabled, true);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_h_mission_plan_store
./build/shared/cpp/tests/test_h_mission_plan_store
```

Expected: FAIL in `storesReceivedMissionPlan()` because the current file writes `message_type: "config"` and no `waypoints`.

- [ ] **Step 3: Write minimal implementation**

In `shared/cpp/src/mission/mission_plan_store.cpp`, include the H protocol conversion header:

```cpp
#include "h_problem_core/protocol/envelope_builder.h"
```

Change `storeMissionPlan()` to validate H invariants, convert to `competition::TaskPlan`, and call canonical storage:

```cpp
bool storeMissionPlan(const MissionPlan &plan, const QString &output_path, QString *error_message) {
    QString validation_error;
    if (!validateMissionPlan(plan, &validation_error)) {
        if (error_message != nullptr) {
            *error_message = validation_error;
        }
        return false;
    }

    return competition::storeTaskPlan(taskPlanFromMissionPlan(plan), output_path, error_message);
}
```

Change `loadMissionPlan()` to first read as canonical `TaskPlan`, then fall back to legacy H JSON:

```cpp
std::optional<MissionPlan> loadMissionPlan(const QString &path, QString *error_message) {
    QString task_plan_error;
    const auto task_plan = competition::loadTaskPlan(path, &task_plan_error);
    if (task_plan.has_value()) {
        return missionPlanFromTaskPlan(task_plan.value(), error_message);
    }

    const auto object = readJsonObject(path, "mission plan", error_message);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return missionPlanFromJsonObject(object.value(), error_message);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target test_h_mission_plan_store
./build/shared/cpp/tests/test_h_mission_plan_store
```

Expected: PASS for all H mission plan store tests.

### Task 2: Preserve H MissionPlan validation at the conversion seam

**Files:**
- Modify: `shared/cpp/tests/test_h_envelope_builder.cpp`
- Modify: `shared/cpp/src/protocol/envelope_builder.cpp`

**Interfaces:**
- Consumes: `hcore::taskPlanFromMissionPlan(const MissionPlan &)`
- Consumes: `hcore::missionPlanFromTaskPlan(const competition::TaskPlan &, QString *)`
- Produces: route/no-fly/landing metadata round-trip through canonical `TaskPlan`

- [ ] **Step 1: Write the failing test**

In `shared/cpp/tests/test_h_envelope_builder.cpp`, add a test that round-trips landing and no-fly metadata through `competition::TaskPlan`:

```cpp
void roundTripsMissionPlanThroughCanonicalTaskPlan();
```

Implementation:

```cpp
void HEnvelopeBuilderTests::roundTripsMissionPlanThroughCanonicalTaskPlan() {
    hcore::MissionPlan plan;
    plan.case_id = "demo";
    plan.start_cell = "A9B1";
    plan.no_fly_cells = {"A1B2", "A2B2", "A3B2"};
    plan.route = {"A9B1", "A8B1", "A7B1"};
    plan.terminal_cell = "A7B1";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 450.0;
    plan.takeoff_anchor_y_cm = 350.0;

    const competition::TaskPlan task_plan = hcore::taskPlanFromMissionPlan(plan);
    QCOMPARE(task_plan.task_id, QString("demo"));
    QCOMPARE(task_plan.task_type, QString("h_problem"));
    QCOMPARE(task_plan.waypoints.size(), 3);
    QCOMPARE(task_plan.waypoints.at(1).id, QString("A8B1"));

    QString error;
    const auto decoded = hcore::missionPlanFromTaskPlan(task_plan, &error);
    QVERIFY2(decoded.has_value(), qPrintable(error));
    QCOMPARE(decoded->case_id, plan.case_id);
    QCOMPARE(decoded->no_fly_cells, plan.no_fly_cells);
    QCOMPARE(decoded->route, plan.route);
    QCOMPARE(decoded->terminal_cell, plan.terminal_cell);
    QCOMPARE(decoded->landing_enabled, true);
    QCOMPARE(decoded->descent_angle_deg.value_or(0.0), 45.0);
    QCOMPARE(decoded->takeoff_anchor_x_cm.value_or(0.0), 450.0);
    QCOMPARE(decoded->takeoff_anchor_y_cm.value_or(0.0), 350.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_h_envelope_builder
./build/shared/cpp/tests/test_h_envelope_builder
```

Expected: If the slot is not declared or metadata is incomplete, FAIL. If it already passes, add one assertion for `task_plan.metadata_json` containing `terminal_cell` and rerun to verify coverage.

- [ ] **Step 3: Write minimal implementation**

If the test fails because a metadata field is missing, update `taskPlanFromMissionPlan()` / `missionPlanFromTaskPlan()` in `shared/cpp/src/protocol/envelope_builder.cpp` so these fields round-trip:

```cpp
metadata["terminal_cell"] = plan.terminal_cell;
metadata["landing_enabled"] = plan.landing_enabled;
metadata["descent_angle_deg"] = plan.descent_angle_deg.has_value() ? QJsonValue(plan.descent_angle_deg.value()) : QJsonValue::Null;
metadata["takeoff_anchor_x_cm"] = plan.takeoff_anchor_x_cm.has_value() ? QJsonValue(plan.takeoff_anchor_x_cm.value()) : QJsonValue::Null;
metadata["takeoff_anchor_y_cm"] = plan.takeoff_anchor_y_cm.has_value() ? QJsonValue(plan.takeoff_anchor_y_cm.value()) : QJsonValue::Null;
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target test_h_envelope_builder
./build/shared/cpp/tests/test_h_envelope_builder
```

Expected: PASS.

### Task 3: Update documentation to state the canonical model rule

**Files:**
- Modify: `docs/framework_architecture.md`
- Modify: `docs/adding_task_adapter.md`
- Modify: `shared/cpp/README.md`

**Interfaces:**
- Consumes: completed Tasks 1-2 behavior
- Produces: documented architecture rule that persisted runtime mission plans are canonical `competition::TaskPlan`

- [ ] **Step 1: Write the failing documentation check**

Add a focused assertion to `ground_station_computer/tests/test_architecture_boundaries.cpp` or `shared/cpp/tests/test_task_ports.cpp` that checks the repository documentation mentions canonical `TaskPlan` storage:

```cpp
void documentsCanonicalTaskPlanStorage();
```

Use `QFile` to read `docs/framework_architecture.md` and verify it contains:

```cpp
QVERIFY(contents.contains("运行时任务计划持久化统一使用 competition::TaskPlan"));
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_architecture_boundaries
./build/ground_station_computer/tests/test_architecture_boundaries
```

Expected: FAIL because the exact rule is not documented yet.

- [ ] **Step 3: Update docs**

In `docs/framework_architecture.md`, add this line under `competition_core`:

```markdown
- 运行时任务计划持久化统一使用 `competition::TaskPlan`；题目专有模型只能作为规划算法内部结构，落盘和传输前必须转换到通用模型。
```

In `docs/adding_task_adapter.md`, add this line under `共享核心`:

```markdown
- 新题目的运行时任务文件必须使用通用 `competition::TaskPlan` JSON；题目私有字段进入 `metadata_json` / `payload_json`，不要新增第二套长期任务计划存储格式。
```

In `shared/cpp/README.md`, add a short note:

```markdown
运行时任务计划以 `competition::TaskPlan` 作为唯一规范模型；各题目核心可保留内部规划结构，但持久化和协议传输必须通过通用模型。
```

- [ ] **Step 4: Run documentation check to verify it passes**

Run:

```bash
cmake --build build --target test_architecture_boundaries
./build/ground_station_computer/tests/test_architecture_boundaries
```

Expected: PASS.

### Task 4: Run affected and full verification

**Files:**
- No source edits unless verification exposes a defect.

**Interfaces:**
- Consumes: Tasks 1-3
- Produces: verified migration state

- [ ] **Step 1: Run affected tests**

Run:

```bash
cmake --build build --target test_h_mission_plan_store test_h_envelope_builder test_task_protocol test_architecture_boundaries
./build/shared/cpp/tests/test_h_mission_plan_store
./build/shared/cpp/tests/test_h_envelope_builder
./build/shared/cpp/tests/test_task_protocol
./build/ground_station_computer/tests/test_architecture_boundaries
```

Expected: all PASS.

- [ ] **Step 2: Run full test suite**

Run:

```bash
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: build exit 0 and ctest reports 0 failed tests.

- [ ] **Step 3: Inspect architectural guardrails**

Run:

```bash
rg -n "storeMissionPlan|loadMissionPlan|missionPlanToJson|missionPlanFromJsonObject" shared/cpp ground_station_computer/src ground_station_computer/tests
rg -n "h_problem_core|h_problem/" shared/cpp/include/competition_core shared/cpp/src/task shared/cpp/src/protocol
```

Expected: H compatibility functions remain only in `h_problem_core`; `competition_core` does not include H headers.

---

## Self-Review

- Spec coverage: The plan covers canonical `TaskPlan` storage, H compatibility, docs, and verification. It does not implement RouteRequest, command FSM, JSON codec consolidation, or simulation event stream; those are intentionally separate follow-up plans.
- Placeholder scan: No TBD/TODO/later placeholders remain.
- Type consistency: The plan uses existing `hcore::MissionPlan`, `competition::TaskPlan`, `taskPlanFromMissionPlan()`, `missionPlanFromTaskPlan()`, `storeMissionPlan()`, and `loadMissionPlan()` names consistently.
