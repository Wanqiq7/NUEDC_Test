# 地面站速度任务契约实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `NUEDC_Test` 生成、校验、展示并模拟米制 `takeoff -> navigate -> land` H 题任务计划，为机载速度闭环提供唯一且可拒绝旧版本的任务契约。

**Architecture:** 规划器、禁飞区和 UI 几何继续使用现有厘米坐标，只在 `buildTaskPlan()` 边界转换为以 A9B1 格心为原点的米制执行坐标。`HProtocolAdapter` 负责 H 题契约校验和“巡查路线/真实触地点”分离，通用 Protobuf 与 `competition::TaskPlan` 不增加字段。

**Tech Stack:** C++17、Qt 6 Core/Widgets/Test、CMake/CTest、Protobuf、JSON runtime fixture。

## Global Constraints

- 参考设计：`docs/superpowers/specs/2026-07-16-airborne-velocity-mission-control-design.md`。
- 保持 `shared/proto/messages.proto` 字段号与序列化格式不变。
- 地面站内部 `cellCenterCm()`、禁飞区、降落几何和带 `_cm` 后缀 metadata 继续使用厘米。
- `TaskWaypoint.x/y/z` 从本计划起统一表示场地系米制执行坐标。
- 场地原点为 A9B1 格心，`+X: B1 -> B7`，`+Y: A9 -> A1`，`+Z` 向上。
- H 题 `metadata_json.execution_contract` 固定为 `h_field_m_v1`；旧计划必须在同步前被拒绝。
- 执行序列固定为首个 `takeoff`、中间 `navigate`、最后 `touchdown/land`；`terminal_waypoint_id=touchdown`。
- `metadata_json.terminal_cell` 仍表示最后巡查格，不得改成 `touchdown`。
- 降落规划仍可使用 `45°±5°`，地面站不新增机载轨迹角失败语义。
- 当前 `h_mission_controller.cpp/.h` 与 `test_h_mission_controller.cpp` 含用户的检测数据库隔离改动。执行 Task 3 前必须先重新读取 `git diff`；不得回退、暂存或混入这些既有改动，除非其所有者已先单独提交。
- 每项行为先写失败测试并确认 RED，再实现最小代码、确认 GREEN、最后提交。

---

### Task 1: 建立厘米规划点到米制执行点的唯一转换

**Files:**
- Modify: `shared/cpp/include/h_problem_core/planning/mission_geometry.h`
- Modify: `shared/cpp/src/planning/mission_geometry.cpp`
- Create: `shared/cpp/tests/test_h_mission_geometry.cpp`
- Modify: `shared/cpp/CMakeLists.txt`

**Interfaces:**
- Consumes: `hcore::PointCm`，其字段仍为 `x_cm/y_cm`。
- Produces: `hcore::MissionPointM` 与 `fieldPointToMissionMeters(const PointCm&)`。
- Later tasks must call this function for both grid centers and touchdown; no second conversion formula is allowed.

- [ ] **Step 1: Write the failing coordinate tests**

Create `test_h_mission_geometry.cpp` with these exact cases:

```cpp
#include <QtTest/QtTest>

#include "h_problem_core/planning/mission_geometry.h"

class HMissionGeometryTests : public QObject {
    Q_OBJECT

private slots:
    void convertsFieldCentimetersToMissionMeters();
};

void HMissionGeometryTests::convertsFieldCentimetersToMissionMeters() {
    struct Case {
        hcore::PointCm input;
        double expected_x;
        double expected_y;
    };
    const QVector<Case> cases{
        {{450.0, 350.0}, 0.0, 0.0},
        {{50.0, 350.0}, 0.0, 4.0},
        {{450.0, 50.0}, 3.0, 0.0},
        {{50.0, 50.0}, 3.0, 4.0},
        {{440.0, 330.0}, 0.2, 0.1},
    };

    for (const Case &item : cases) {
        const hcore::MissionPointM point = hcore::fieldPointToMissionMeters(item.input);
        QCOMPARE(point.x_m, item.expected_x);
        QCOMPARE(point.y_m, item.expected_y);
    }
}

QTEST_MAIN(HMissionGeometryTests)
#include "test_h_mission_geometry.moc"
```

Register it in `shared/cpp/CMakeLists.txt`:

```cmake
add_h_problem_core_test(test_h_mission_geometry tests/test_h_mission_geometry.cpp)
```

- [ ] **Step 2: Run the new target and verify RED**

```bash
cmake -S . -B build
cmake --build build --target test_h_mission_geometry
```

Expected: compilation fails because `MissionPointM` and `fieldPointToMissionMeters` do not exist.

- [ ] **Step 3: Add the focused conversion interface and implementation**

Add to `mission_geometry.h`:

```cpp
struct MissionPointM {
    double x_m = 0.0;
    double y_m = 0.0;
};

MissionPointM fieldPointToMissionMeters(const PointCm &point_cm);
```

Add to `mission_geometry.cpp` without changing `cellCenterCm()`:

```cpp
MissionPointM fieldPointToMissionMeters(const PointCm &point_cm) {
    return {
        (350.0 - point_cm.y_cm) / 100.0,
        (450.0 - point_cm.x_cm) / 100.0,
    };
}
```

- [ ] **Step 4: Verify GREEN**

```bash
cmake --build build --target test_h_mission_geometry
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^test_h_mission_geometry$' --output-on-failure
```

Expected: `1/1` test passes.

- [ ] **Step 5: Commit the conversion primitive**

```bash
git add shared/cpp/CMakeLists.txt \
  shared/cpp/include/h_problem_core/planning/mission_geometry.h \
  shared/cpp/src/planning/mission_geometry.cpp \
  shared/cpp/tests/test_h_mission_geometry.cpp
git commit -m "feat: add H mission coordinate conversion"
```

### Task 2: 原子切换 TaskPlan 生成与 H 题契约校验

**Files:**
- Modify: `shared/cpp/include/h_problem_core/mission/mission_planning.h`
- Modify: `shared/cpp/src/mission/mission_planning.cpp`
- Modify: `shared/cpp/tests/test_h_case_loader.cpp`
- Modify: `shared/cpp/tests/test_task_protocol.cpp`
- Modify: `ground_station_computer/src/h_problem/mission/h_protocol_adapter.cpp`
- Create: `ground_station_computer/tests/test_h_protocol_adapter.cpp`
- Modify: `ground_station_computer/CMakeLists.txt`

**Interfaces:**
- Consumes: `fieldPointToMissionMeters()` from Task 1 and the existing `competition::TaskPlan`/Protobuf codec.
- Produces: `inline constexpr char HExecutionContract[] = "h_field_m_v1";` and `HTouchdownWaypointId[] = "touchdown"`.
- Keeps these public signatures unchanged:

```cpp
std::optional<competition::TaskPlan> buildTaskPlan(
    const CaseConfig &case_config,
    std::optional<QStringList> override_no_fly_cells = std::nullopt,
    QString *error_message = nullptr);
bool HProtocolAdapter::validateTaskPlan(
    const competition::TaskPlan &plan, QString *error_message = nullptr);
bool HProtocolAdapter::decodeTaskPlan(
    const competition::TaskPlan &plan, HGridConfigData *data,
    QString *error_message = nullptr);
```
- Produces: `HGridConfigData::route` containing only takeoff+navigate grid IDs; touchdown remains in its existing numeric fields.

- [ ] **Step 1: Add failing generator and round-trip assertions**

Extend `buildTaskPlanProducesCanonicalPlanWithLandingMetadata()` in `test_h_case_loader.cpp` with:

```cpp
QCOMPARE(plan->start_waypoint_id, QString("A9B1"));
QCOMPARE(plan->terminal_waypoint_id, QString("touchdown"));
QVERIFY(plan->waypoints.size() >= 3);
QCOMPARE(plan->waypoints.first().id, QString("A9B1"));
QCOMPARE(plan->waypoints.first().action, QString("takeoff"));
QCOMPARE(plan->waypoints.first().x, 0.0);
QCOMPARE(plan->waypoints.first().y, 0.0);
QCOMPARE(plan->waypoints.first().z, 1.2);
QCOMPARE(plan->waypoints.last().id, QString("touchdown"));
QCOMPARE(plan->waypoints.last().action, QString("land"));
QCOMPARE(plan->waypoints.last().z, 0.0);

const QJsonObject metadata = QJsonDocument::fromJson(plan->metadata_json.toUtf8()).object();
QCOMPARE(metadata.value("execution_contract").toString(), QString("h_field_m_v1"));
QCOMPARE(metadata.value("cruise_height_cm").toDouble(), 120.0);
QCOMPARE(metadata.value("terminal_cell").toString(),
         plan->waypoints.at(plan->waypoints.size() - 2).id);
```

Extend `buildsAndParsesTaskPlanEnvelope()` in `test_task_protocol.cpp` so its fixture contains `takeoff`, `navigate`, and `land`, then assert:

```cpp
const competition::TaskPlan original = makeTaskPlan();
const Envelope envelope = competition::buildTaskPlanEnvelope(42, original);
QString error;
const auto parsed = competition::taskPlanFromMessage(envelope.task_plan(), &error);
QVERIFY2(parsed.has_value(), qPrintable(error));

QCOMPARE(parsed->metadata_json, original.metadata_json);
QCOMPARE(parsed->terminal_waypoint_id, QString("touchdown"));
QCOMPARE(parsed->waypoints.size(), original.waypoints.size());
for (int index = 0; index < original.waypoints.size(); ++index) {
    const auto &expected = original.waypoints.at(index);
    const auto &actual = parsed->waypoints.at(index);
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.sequence_index, expected.sequence_index);
    QCOMPARE(actual.action, expected.action);
    QCOMPARE(actual.x, expected.x);
    QCOMPARE(actual.y, expected.y);
    QCOMPARE(actual.z, expected.z);
    QCOMPARE(actual.payload_json, expected.payload_json);
}
```

In the same test, store `original` through `competition::storeTaskPlan()` into a `QTemporaryDir`,
reload with `competition::loadTaskPlan()`, and run the identical comparison helper against both
the Protobuf-decoded and JSON-loaded plans. The helper must compare task ID/type, metadata JSON,
start and terminal IDs, waypoint count, and every waypoint's ID, sequence, action, XYZ, and
payload JSON. This locks the complete Protobuf and task-plan-store round trips rather than only
the newly added fields.

- [ ] **Step 2: Create focused failing H protocol tests**

Create `test_h_protocol_adapter.cpp` with a helper that loads `sample_case.json` through `buildTaskPlan()`, and add these slots:

```cpp
private slots:
    void acceptsGeneratedExecutablePlan();
    void rejectsMissingWrongAndNonStringExecutionContract();
    void decodedRouteExcludesTouchdown();
    void rejectsNonFiniteCoordinates();
    void rejectsUnknownAction();
    void rejectsTakeoffOutsideFirstWaypoint();
    void rejectsNonA9B1Start();
    void rejectsRouteCoordinateMismatch();
    void rejectsLandMetadataMismatch();
```

Use a data-driven `rejectsMissingWrongAndNonStringExecutionContract_data()` table containing malformed JSON, missing key, wrong string, null, number, and object. For each row, require both `validateTaskPlan()` and `decodeTaskPlan()` to fail with a nonempty error. The central assertions are:

```cpp
HGridConfigData decoded;
QString error;
QVERIFY2(HProtocolAdapter::decodeTaskPlan(plan, &decoded, &error), qPrintable(error));
QVERIFY(!decoded.route.contains("touchdown"));
QCOMPARE(decoded.route.last(), metadata.value("terminal_cell").toString());

competition::TaskPlan missing_contract = plan;
QJsonObject missing_metadata = metadata;
missing_metadata.remove("execution_contract");
missing_contract.metadata_json = QString::fromUtf8(
    QJsonDocument(missing_metadata).toJson(QJsonDocument::Compact));
QVERIFY(!HProtocolAdapter::validateTaskPlan(missing_contract, &error));

competition::TaskPlan non_finite = plan;
non_finite.waypoints[1].x = std::numeric_limits<double>::infinity();
QVERIFY(!HProtocolAdapter::validateTaskPlan(non_finite, &error));
```

Initialize `HGridConfigData decoded` with sentinel route/endpoints before each invalid row and assert `decodeTaskPlan()` leaves it unchanged. The controller already returns immediately when decode fails; Task 3's existing `rejectsMalformedTaskPlanWithoutChangingDisplayedRoute()` remains the UI side-effect guard without adding new edits to its currently dirty test file.

Register the test:

```cmake
add_ground_station_test(test_h_protocol_adapter tests/test_h_protocol_adapter.cpp)
```

- [ ] **Step 3: Run focused targets and verify RED**

```bash
cmake --build build --target test_h_case_loader test_task_protocol test_h_protocol_adapter
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^(test_h_case_loader|test_task_protocol|test_h_protocol_adapter)$' \
  --output-on-failure
```

Expected: generator assertions fail because the current plan is centimeter/all-navigate, and the new adapter test fails because no contract or land exists.

- [ ] **Step 4: Implement executable plan generation**

Add to `mission_planning.h`:

```cpp
inline constexpr char HExecutionContract[] = "h_field_m_v1";
inline constexpr char HTouchdownWaypointId[] = "touchdown";
```

In `buildTaskPlan()`, reject a non-A9B1 H start and build waypoints with this exact shape:

```cpp
if (case_config.start_cell != "A9B1") {
    if (error_message != nullptr) {
        *error_message = "H mission start_cell must be A9B1";
    }
    return std::nullopt;
}

metadata["execution_contract"] = HExecutionContract;
metadata["cruise_height_cm"] = case_config.landing->cruise_height_cm;
const double cruise_height_m = case_config.landing->cruise_height_cm / 100.0;
plan.metadata_json = compactJson(metadata);

for (int index = 0; index < route_result.route.size(); ++index) {
    const QString cell = route_result.route.at(index);
    const auto center_cm = cellCodeCenterCm(cell, MapHeight);
    if (!center_cm.has_value()) {
        if (error_message != nullptr) {
            *error_message = QString("planner returned invalid route cell: %1").arg(cell);
        }
        return std::nullopt;
    }
    const MissionPointM center_m = fieldPointToMissionMeters(center_cm.value());
    competition::TaskWaypoint waypoint;
    waypoint.id = cell;
    waypoint.sequence_index = static_cast<quint32>(index);
    waypoint.x = center_m.x_m;
    waypoint.y = center_m.y_m;
    waypoint.z = cruise_height_m;
    waypoint.action = index == 0 ? "takeoff" : "navigate";
    waypoint.payload_json = compactJson(QJsonObject{{"cell", cell}});
    plan.waypoints.append(waypoint);
}

const MissionPointM touchdown_m =
    fieldPointToMissionMeters(landing_approach->touchdown_point_cm);
plan.waypoints.append({
    HTouchdownWaypointId,
    static_cast<quint32>(plan.waypoints.size()),
    touchdown_m.x_m,
    touchdown_m.y_m,
    0.0,
    "land",
    compactJson(QJsonObject{{"touchdown", true}}),
});
plan.terminal_waypoint_id = HTouchdownWaypointId;
```

Move the existing `plan.metadata_json = compactJson(metadata)` assignment to the shown position if necessary; serialization must happen after `execution_contract` is inserted.

- [ ] **Step 5: Implement H-specific validation and route extraction**

Keep existing metadata parsing and replace the all-grid loop with three collections:

```cpp
QStringList route;
const competition::TaskWaypoint *land = nullptr;
for (int index = 0; index < plan.waypoints.size(); ++index) {
    const competition::TaskWaypoint &waypoint = plan.waypoints.at(index);
    if (waypoint.sequence_index != static_cast<quint32>(index) ||
        !std::isfinite(waypoint.x) || !std::isfinite(waypoint.y) ||
        !std::isfinite(waypoint.z)) {
        return reject(error_message, "H 题航点序号和坐标必须有效");
    }
    if (waypoint.action == "takeoff" || waypoint.action == "navigate") {
        if (!isGridCell(waypoint.id)) {
            return reject(error_message, "H 题巡查航点必须是合法网格");
        }
        route.append(waypoint.id);
    } else if (waypoint.action == "land") {
        if (land != nullptr) {
            return reject(error_message, "H 题只能包含一个 land 航点");
        }
        land = &waypoint;
    } else {
        return reject(error_message, "H 题航点 action 非法");
    }
}

if (metadata->value("execution_contract").toString() != HExecutionContract ||
    route.isEmpty() || plan.waypoints.first().action != "takeoff" ||
    land == nullptr || land != &plan.waypoints.last() ||
    land->id != HTouchdownWaypointId ||
    plan.terminal_waypoint_id != HTouchdownWaypointId) {
    return reject(error_message, "H 题执行契约不匹配");
}
```

Before that check, require `metadata.execution_contract` to be a JSON string; do not call `toString()` as the only type check because null/number/object values would collapse to the same empty fallback as a missing key. Require a finite positive numeric `metadata.cruise_height_cm`. After the structural check, require every `route` entry after index 0 to have `action == "navigate"`; this rejects a second `takeoff` in the middle. Require `route.first() == "A9B1"`, `plan.start_waypoint_id == route.first()`, `metadata.start_cell == route.first()`, and `metadata.terminal_cell == route.last()`. For every route waypoint, recompute its expected center through `cellCodeCenterCm()` and `fieldPointToMissionMeters()` and compare `x/y` within `ValidationEpsilon`; require its `z == metadata.cruise_height_cm / 100.0` within the same tolerance. Do not derive cruise height from `descent_run_cm * tan(descent_angle_deg)`: the planner may choose a run within the allowed angle tolerance, so nominal angle and actual run do not reconstruct the configured height. When reconstructing `LandingProfile` inside existing landing validation, set `cruise_height_cm` directly from metadata and preserve the contract tolerance (`descent_angle_tolerance_deg = 5.0`); do not force tolerance to zero or recompute height from the chosen run. Run coverage, adjacency, no-fly, and `terminal_cell` checks only on `route`. Convert metadata touchdown through `fieldPointToMissionMeters()` and compare it to `land->x/y`; require `land->z == 0.0`. In `decodeTaskPlan()`, append only takeoff/navigate IDs to `data->route`.

- [ ] **Step 6: Verify GREEN and commit the atomic contract change**

```bash
cmake --build build --target test_h_case_loader test_task_protocol test_h_protocol_adapter test_h_mission_controller
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^(test_h_case_loader|test_task_protocol|test_h_protocol_adapter|test_h_mission_controller)$' \
  --output-on-failure
git add shared/cpp/include/h_problem_core/mission/mission_planning.h \
  shared/cpp/src/mission/mission_planning.cpp \
  shared/cpp/tests/test_h_case_loader.cpp \
  shared/cpp/tests/test_task_protocol.cpp \
  ground_station_computer/CMakeLists.txt \
  ground_station_computer/src/h_problem/mission/h_protocol_adapter.cpp \
  ground_station_computer/tests/test_h_protocol_adapter.cpp
git commit -m "feat: emit executable H task plans"
```

Expected: all four focused targets pass; `messages.proto` has no diff.

### Task 3: 分离下降起点与真实触地点的 UI 语义

**Files:**
- Modify: `ground_station_computer/src/h_problem/mission/h_mission_view_sink.h`
- Modify: `ground_station_computer/src/h_problem/mission/h_mission_controller.cpp`
- Modify: `ground_station_computer/src/h_problem/ui/h_problem_view.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_problem_view.cpp`
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.cpp`
- Modify: `ground_station_computer/tests/test_grid_scene.cpp`
- Modify: `ground_station_computer/tests/test_h_mission_controller.cpp`

**Interfaces:**
- Consumes: filtered `HGridConfigData::route`, `terminal_cell`, and touchdown centimeter fields from Task 2.
- Produces the full `showRoute()` and `GridScene::setLandingTarget()` signatures shown in Steps 3 and 4, with `descent_start_cell/touchdown_x_cm/touchdown_y_cm` semantics.
- Preserves: all existing detection database injection and per-task clearing behavior in the dirty controller files.

- [ ] **Step 1: Protect the existing user changes and add failing UI tests**

Before editing, record the existing diff without changing it:

```bash
git diff -- ground_station_computer/src/h_problem/mission/h_mission_controller.cpp \
  ground_station_computer/src/h_problem/mission/h_mission_controller.h \
  ground_station_computer/tests/test_h_mission_controller.cpp
```

Include `QGraphicsLineItem` and `QGraphicsSimpleTextItem`. Add `drawsSeparateDescentStartAndTouchdownMarkers()`, `zeroTouchdownCoordinatesRemainValid()`, and `clearsLandingItemsWhenDisabled()` to the class's `private slots`. Add stable item data keys for descent and touchdown markers, then extend `test_grid_scene.cpp` with:

```cpp
constexpr int kLandingRoleDataKey = 1002;

QGraphicsItem *findItemByRole(const GridScene &scene, const QString &role) {
    for (QGraphicsItem *item : scene.items()) {
        if (item->data(kLandingRoleDataKey).toString() == role) {
            return item;
        }
    }
    return nullptr;
}

QGraphicsSimpleTextItem *findSimpleText(
    const GridScene &scene, const QString &text) {
    for (QGraphicsItem *item : scene.items()) {
        auto *label = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
        if (label != nullptr && label->text() == text) {
            return label;
        }
    }
    return nullptr;
}

void GridSceneTests::drawsSeparateDescentStartAndTouchdownMarkers() {
    TestableGridScene scene;
    scene.setLandingTarget("A8B3", 260.0, 190.0, true);

    QVERIFY(findItemByRole(scene, "descent_start_marker") != nullptr);
    QVERIFY(findItemByRole(scene, "touchdown_marker") != nullptr);
    QVERIFY(findItemByRole(scene, "landing_corridor") != nullptr);
    QVERIFY(scene.items().contains(findSimpleText(scene, QStringLiteral("下降起点"))));
    QVERIFY(scene.items().contains(findSimpleText(scene, QStringLiteral("降落终点"))));
}

void GridSceneTests::zeroTouchdownCoordinatesRemainValid() {
    TestableGridScene scene;
    scene.setLandingTarget("A9B1", 0.0, 0.0, true);

    auto *touchdown = findItemByRole(scene, "touchdown_marker");
    QVERIFY(touchdown != nullptr);
    QCOMPARE(touchdown->sceneBoundingRect().center(),
             QPointF(((0.0 - 25.0) / 50.0) * 52.0,
                     ((0.0 - 25.0) / 50.0) * 52.0));
}

void GridSceneTests::clearsLandingItemsWhenDisabled() {
    TestableGridScene scene;
    scene.setLandingTarget("A8B3", 260.0, 190.0, true);
    scene.setLandingTarget({}, 0.0, 0.0, false);

    QVERIFY(findItemByRole(scene, "descent_start_marker") == nullptr);
    QVERIFY(findItemByRole(scene, "touchdown_marker") == nullptr);
    QVERIFY(findItemByRole(scene, "landing_corridor") == nullptr);
    QVERIFY(findSimpleText(scene, QStringLiteral("下降起点")) == nullptr);
    QVERIFY(findSimpleText(scene, QStringLiteral("降落终点")) == nullptr);
}
```

In the nonzero case, cast `landing_corridor` to `QGraphicsLineItem` and compare both endpoints with `testCellCenter("A8B3")` and the expected scene point for `(260,190)`. Make the zero-coordinate case data-driven over `(0,0)`, `(0,190)`, and `(260,0)` so neither axis can regress to a sentinel. Re-run `setLandingTarget()` with a different target and assert the old marker/labels are removed rather than accumulated.

Update the recording sink assertion so `last_route` excludes `touchdown`, `last_terminal` is the last navigate cell, and the touchdown values are passed unchanged even when either is zero.

- [ ] **Step 2: Run focused UI tests and verify RED**

```bash
cmake --build build --target test_grid_scene test_h_mission_controller test_main_window
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^(test_grid_scene|test_h_mission_controller|test_main_window)$' \
  --output-on-failure
```

Expected: tests fail because only the last patrol cell is currently marked and zero is treated as missing.

- [ ] **Step 3: Rename the narrow view interface and remove zero sentinels**

Use this exact signature in the sink and view:

```cpp
virtual void showRoute(
    const QStringList &no_fly_cells,
    const QStringList &route,
    const QString &start_cell,
    const QString &descent_start_cell,
    double touchdown_x_cm,
    double touchdown_y_cm,
    bool landing_enabled) = 0;
```

Both controller call sites must pass the validated values directly:

```cpp
sink_->showRoute(
    no_fly_cells,
    route,
    start_cell,
    terminal_cell,
    touchdown_x_cm,
    touchdown_y_cm,
    landing_enabled);
```

Delete both `coordinate != 0.0 ? coordinate : takeoff_anchor` expressions.

- [ ] **Step 4: Draw both landing semantics in GridScene**

Change the public method to:

```cpp
void setLandingTarget(
    const QString &descent_start_cell,
    double touchdown_x_cm,
    double touchdown_y_cm,
    bool enabled);
```

Keep distinct members and labels:

```cpp
QGraphicsEllipseItem *descent_start_marker_ = nullptr;
QGraphicsSimpleTextItem *descent_start_label_ = nullptr;
QGraphicsEllipseItem *touchdown_marker_ = nullptr;
QGraphicsSimpleTextItem *touchdown_label_ = nullptr;
QGraphicsLineItem *landing_corridor_ = nullptr;
```

The marker positions and line must use:

```cpp
const QPointF descent_start = cellCenter(descent_start_cell);
const QPointF touchdown = fieldPointToScene(touchdown_x_cm, touchdown_y_cm);
descent_start_marker_->setPos(descent_start);
touchdown_marker_->setPos(touchdown);
landing_corridor_->setLine(QLineF(descent_start, touchdown));
```

Label them exactly `下降起点` and `降落终点`. Set item data roles used by the tests; do not infer field presence from coordinate values.

Use the same `kLandingRoleDataKey = 1002` in `h_grid_scene.cpp` and set exact values `descent_start_marker`, `touchdown_marker`, and `landing_corridor` on the three items. The test and implementation constants are a deliberate stable UI-test contract.

- [ ] **Step 5: Verify GREEN and commit only after dirty overlap is resolved**

```bash
cmake --build build --target test_grid_scene test_h_mission_controller test_main_window
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^(test_grid_scene|test_h_mission_controller|test_main_window)$' \
  --output-on-failure
git diff --check
```

Expected: all three tests pass and the pre-existing detection isolation tests remain present. Once the owner has separately committed the original dirty hunks, commit this task:

```bash
git add ground_station_computer/src/h_problem/mission/h_mission_view_sink.h \
  ground_station_computer/src/h_problem/mission/h_mission_controller.cpp \
  ground_station_computer/src/h_problem/ui/h_problem_view.h \
  ground_station_computer/src/h_problem/ui/h_problem_view.cpp \
  ground_station_computer/src/h_problem/ui/h_grid_scene.h \
  ground_station_computer/src/h_problem/ui/h_grid_scene.cpp \
  ground_station_computer/tests/test_grid_scene.cpp \
  ground_station_computer/tests/test_h_mission_controller.cpp
git commit -m "ui: distinguish descent start and touchdown"
```

### Task 4: 让模拟事件与 runtime fixture 遵循完整 sequence

**Files:**
- Modify: `shared/cpp/src/runtime/simulator.cpp`
- Modify: `shared/cpp/tests/test_h_simulator.cpp`
- Create: `shared/cpp/tools/generate_h_runtime_fixture.cpp`
- Modify: `shared/cpp/CMakeLists.txt`
- Modify: `scripts/mock_airborne.py`
- Modify: `runtime/mock_airborne_active_mission_plan.json`
- Modify: `ground_station_computer/tests/test_h_protocol_adapter.cpp`

**Interfaces:**
- Consumes: Task 2 action/sequence contract.
- Produces: visited count deduplicated by `sequence_index`, including takeoff/navigate/land.
- Keeps: telemetry `current_cell` as the last valid grid cell during land; `touchdown` is never decoded as a grid cell.

- [ ] **Step 1: Add failing simulator sequence tests**

Replace the provided-plan fixture in `test_h_simulator.cpp` with:

```cpp
plan.task_id = "plan-test";
plan.task_type = "h_problem";
plan.start_waypoint_id = "A9B1";
plan.terminal_waypoint_id = "touchdown";
plan.waypoints = {
    {"A9B1", 0, 0.0, 0.0, 1.2, "takeoff", R"({"cell":"A9B1"})"},
    {"A8B1", 1, 0.0, 0.5, 1.2, "navigate", R"({"cell":"A8B1"})"},
    {"A9B1", 2, 0.0, 0.0, 1.2, "navigate", R"({"cell":"A9B1"})"},
    {"touchdown", 3, 0.2, 0.2, 0.0, "land", R"({"touchdown":true})"},
};
```

Also change `makeCase().start_cell` from `A1B1` to `A9B1` and move its normal animal fixtures to grid IDs present in the generated route. Add a deliberate `{ "touchdown", "invalid-terminal-animal", 1 }` animal only in the provided-plan test; the old simulator will emit that invalid detection, making the new `detectionCells(...).contains("touchdown") == false` assertion non-vacuous. Task 2 intentionally makes non-A9B1 generation invalid, so the simulator's generated-plan test must use the canonical start.

Assert:

```cpp
QCOMPARE(stream->summary.visited_waypoints, 4U);
QCOMPARE(telemetry_events.last().waypoint_id, QString("touchdown"));
QCOMPARE(telemetryPayload(telemetry_events.last()).value("current_cell").toString(),
         QString("A9B1"));
QVERIFY(detectionCells(stream->events).contains("touchdown") == false);
```

- [ ] **Step 2: Run the simulator test and verify RED**

```bash
cmake --build build --target test_h_simulator
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^test_h_simulator$' --output-on-failure
```

Expected: visited is under-counted by repeated IDs or touchdown becomes the current cell.

- [ ] **Step 3: Count sequences and preserve the last grid cell**

Use these state variables in `simulateTaskStream()`:

```cpp
QSet<quint32> visited_sequences;
QString last_grid_cell;
for (const competition::TaskWaypoint &waypoint : config_payload.waypoints) {
    visited_sequences.insert(waypoint.sequence_index);
    const bool grid_action = waypoint.action == "takeoff" || waypoint.action == "navigate";
    if (grid_action) {
        last_grid_cell = waypoint.id;
    }

    QJsonObject telemetry_payload;
    telemetry_payload["current_cell"] = last_grid_cell;
    telemetry_payload["visited_cells"] = static_cast<int>(visited_sequences.size());
    telemetry_payload["tick_interval_ms"] = case_config.tick_interval_ms;
    stream.events.append({
        stream.plan.task_id,
        "telemetry",
        waypoint.sequence_index,
        waypoint.id,
        compactJson(telemetry_payload),
    });

    if (grid_action && animals_by_cell.contains(waypoint.id) &&
        !reported_detection_cells.contains(waypoint.id)) {
        reported_detection_cells.insert(waypoint.id);
    }
}
stream.summary.visited_waypoints = static_cast<quint32>(visited_sequences.size());
```

Keep the existing detection payload and totals accumulation inside the shown grid-action branch.

- [ ] **Step 4: Add a deterministic fixture generator and update the mock self-test**

Create `shared/cpp/tools/generate_h_runtime_fixture.cpp` so the checked-in fixture uses the real planner and serializer:

```cpp
#include "competition_core/mission/task_plan_store.h"
#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        QTextStream(stderr) << "usage: generate_h_runtime_fixture CASE OUTPUT\n";
        return 2;
    }
    QString error;
    const auto config = hcore::loadCase(application.arguments().at(1), &error);
    if (!config.has_value()) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    const auto plan = hcore::buildTaskPlan(config.value(), std::nullopt, &error);
    if (!plan.has_value() ||
        !competition::storeTaskPlan(plan.value(), application.arguments().at(2), &error)) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    return 0;
}
```

Register it:

```cmake
add_executable(generate_h_runtime_fixture tools/generate_h_runtime_fixture.cpp)
target_link_libraries(generate_h_runtime_fixture PRIVATE
    Qt6::Core
    h_problem_core
    competition_core
)
```

Add `import math` to `scripts/mock_airborne.py`, then validate the H wire contract before changing `task_id`, sequence state, runtime files, or routes:

```python
def validate_h_wire_plan(plan) -> None:
    if plan.task_type != "h_problem":
        return
    try:
        metadata = json.loads(plan.metadata_json)
    except (json.JSONDecodeError, TypeError) as error:
        raise ValueError("invalid H metadata JSON") from error
    if type(metadata) is not dict or type(metadata.get("execution_contract")) is not str:
        raise ValueError("missing H execution contract")
    if metadata["execution_contract"] != "h_field_m_v1":
        raise ValueError("unsupported H execution contract")
    cruise_height_cm = metadata.get("cruise_height_cm")
    if (type(cruise_height_cm) not in (int, float) or
            not math.isfinite(cruise_height_cm) or cruise_height_cm <= 0):
        raise ValueError("invalid H cruise height")

    waypoints = list(plan.waypoints)
    if len(waypoints) < 3:
        raise ValueError("H mission requires takeoff, navigate, and land")
    if (plan.start_waypoint_id != "A9B1" or
            waypoints[0].id != "A9B1" or waypoints[0].action != "takeoff"):
        raise ValueError("H mission must start with A9B1 takeoff")
    if (plan.terminal_waypoint_id != "touchdown" or
            waypoints[-1].id != "touchdown" or waypoints[-1].action != "land"):
        raise ValueError("H mission must end with touchdown land")
    if any(item.action != "navigate" for item in waypoints[1:-1]):
        raise ValueError("H middle waypoints must navigate")
    if metadata.get("terminal_cell") != waypoints[-2].id:
        raise ValueError("H terminal_cell does not match the last patrol waypoint")
    for index, item in enumerate(waypoints):
        if item.sequence_index != index:
            raise ValueError("H waypoint sequence must be contiguous")
        if not all(math.isfinite(value) for value in (item.x, item.y, item.z)):
            raise ValueError("H waypoint coordinates must be finite")
```

Call this function at the top of the `mission_load` branch and return a failed ACK on `ValueError` without accepting the sequence. Keep `self.route` as grid-only display/detection IDs and add `self.execution_waypoints` for the full plan. On successful load, set the former from `takeoff/navigate` and the latter from every waypoint; publish telemetry for all execution waypoints, preserve the last grid cell in `current_cell` during land, and count visited sequences. Never pass `touchdown` to grid-only detection or UI cell payloads. After publishing the one terminal Summary, atomically set `mission_running=false` and mark that execution summarized so the outer loop cannot publish duplicate summaries; a new START creates a new execution and clears this marker. Add a deterministic helper-level test that invokes two or more completion loop iterations without a new START and asserts exactly one Summary envelope was emitted, not merely that a boolean marker was set. Make its self-test mission use A9B1, `h_field_m_v1`, and a terminal land:

```python
plan.start_waypoint_id = "A9B1"
plan.terminal_waypoint_id = "touchdown"
plan.metadata_json = compact_json({
    "execution_contract": "h_field_m_v1",
    "terminal_cell": "A8B1",
    "cruise_height_cm": 120.0,
})
waypoints = (
    ("A9B1", "takeoff", 0.0, 0.0, 1.2),
    ("A8B1", "navigate", 0.0, 0.5, 1.2),
    ("touchdown", "land", 0.2, 0.2, 0.0),
)
for index, (waypoint_id, action, x, y, z) in enumerate(waypoints):
    waypoint = plan.waypoints.add()
    waypoint.id = waypoint_id
    waypoint.sequence_index = index
    waypoint.action = action
    waypoint.x = x
    waypoint.y = y
    waypoint.z = z
```

Before sending the valid load at sequence 10, table-test malformed JSON, missing/wrong contract, and null/number/object contract values at sequence 10. Each must fail, leave `last_accepted_sequence == 0`, and leave the runtime file absent; the following valid load with the same sequence must still succeed.

Run the mock self-test against a temporary file, then generate the checked-in fixture from the actual planner:

```bash
python3 scripts/mock_airborne.py --self-test \
  --runtime-path /tmp/nuedc-mock-self-test-plan.json
cmake --build build --target generate_h_runtime_fixture
./build/shared/cpp/generate_h_runtime_fixture \
  shared/cases/sample_case.json \
  runtime/mock_airborne_active_mission_plan.json
```

Expected: self-test passes; the checked-in JSON contains the full planner metadata,
`execution_contract`, meter coordinates, and a final `touchdown/land`. Extend
`test_h_protocol_adapter.cpp` to load both this checked-in file and a freshly generated canonical
sample plan; require `decodeTaskPlan()` success and compare task ID/type, metadata JSON, start and
terminal IDs, waypoint count, and every waypoint's ID, sequence, action, XYZ, and payload. The
test must generate the fresh plan through the real planner in the same run, so the checked-in
fixture cannot silently drift from current planner output. Add the mock self-test command to the
Task 5 completion gate because it is not a CTest target.

- [ ] **Step 5: Verify GREEN and commit simulator artifacts**

```bash
cmake --build build --target test_h_simulator test_h_protocol_adapter
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^(test_h_simulator|test_h_protocol_adapter)$' \
  --output-on-failure
git add shared/cpp/src/runtime/simulator.cpp \
  shared/cpp/tests/test_h_simulator.cpp \
  shared/cpp/tools/generate_h_runtime_fixture.cpp \
  shared/cpp/CMakeLists.txt \
  scripts/mock_airborne.py \
  runtime/mock_airborne_active_mission_plan.json \
  ground_station_computer/tests/test_h_protocol_adapter.cpp
git commit -m "testbed: align H simulator with executable plans"
```

### Task 5: 更新契约文档并完成地面站全量验收

**Files:**
- Modify: `README.md`
- Modify: `shared/cpp/README.md`
- Modify: `docs/dual_nuc_setup_guide.md`
- Modify: `docs/ground_station_architecture_spec.md`
- Modify: `ground_station_computer/tests/test_architecture_boundaries.cpp`

**Interfaces:**
- Documents the exact deployment contract consumed by the airborne plan.
- Produces no new runtime API.

- [ ] **Step 1: Add a failing architecture-document contract test**

Add `void documentsHVelocityExecutionContract();` to the class's `private slots`, then extend `test_architecture_boundaries.cpp` with:

```cpp
void ArchitectureBoundaryTests::documentsHVelocityExecutionContract() {
const QStringList required_contract_terms{
    "h_field_m_v1",
    "A9B1",
    "+X: B1 -> B7",
    "+Y: A9 -> A1",
    "takeoff -> navigate -> land",
    "terminal_waypoint_id=touchdown",
    "metadata_json.terminal_cell",
};
for (const QString &relative_path : {
         QStringLiteral("README.md"),
         QStringLiteral("shared/cpp/README.md"),
         QStringLiteral("docs/dual_nuc_setup_guide.md"),
         QStringLiteral("docs/ground_station_architecture_spec.md")}) {
    const QString contents = readSourceOrFail(relative_path);
    for (const QString &term : required_contract_terms) {
        QVERIFY2(
            contents.contains(term),
            qPrintable(QString("%1 missing %2").arg(relative_path, term)));
    }
    QVERIFY2(
        !contents.contains("START 仅在飞控 Action 接受后成功"),
        qPrintable(relative_path));
}
}
```

- [ ] **Step 2: Run the boundary test and verify RED**

```bash
cmake --build build --target test_architecture_boundaries
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^test_architecture_boundaries$' --output-on-failure
```

Expected: required contract terms are absent or old Action wording remains.

- [ ] **Step 3: Update all four documents with the same contract block**

Use this exact content, adapting only surrounding headings. In `ground_station_architecture_spec.md`, replace the existing `HMissionViewSink::showRoute`/landing endpoint description so it matches the new descent-start and touchdown parameters; do not merely append a second contradictory block:

```text
H 题执行契约为 h_field_m_v1。TaskWaypoint 使用米，A9B1 格心为原点，
+X: B1 -> B7，+Y: A9 -> A1。执行序列为 takeoff -> navigate -> land；
terminal_waypoint_id=touchdown 表示最终落点，metadata_json.terminal_cell
表示最后巡查格。START 由机载 mission_coordinator 直接接受并运行速度闭环，
不再调用 /nuedc/execute_mission Action。
```

Document the atomic deployment rule: do not deploy this ground contract until the airborne plan's final system test passes.

- [ ] **Step 4: Run full ground verification**

```bash
cmake -S . -B build
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 scripts/mock_airborne.py --self-test \
  --runtime-path /tmp/nuedc-mock-self-test-plan.json
git diff --check
```

Expected: all configured tests and the mock protocol self-test pass; no generated file under `build/` is staged.

- [ ] **Step 5: Commit documentation and verification guard**

```bash
git add README.md shared/cpp/README.md \
  docs/dual_nuc_setup_guide.md \
  docs/ground_station_architecture_spec.md \
  ground_station_computer/tests/test_architecture_boundaries.cpp
git commit -m "docs: document H velocity mission contract"
```

## Completion Gate

The ground repository is complete only when Tasks 1-5 are committed, the full CTest suite passes, and the generated runtime fixture is accepted by `HProtocolAdapter`. Do not deploy it alone: the airborne `execution_contract` LOAD gate and velocity controller must ship in the same deployment window.

Record this implementation plan in a dedicated task-scoped commit before implementation commits:

```bash
git add docs/superpowers/plans/2026-07-16-ground-velocity-mission-contract.md
git commit -m "docs: plan ground velocity mission contract"
```

Do not use `git add -A`; preserve unrelated controller changes until their owner resolves them.
