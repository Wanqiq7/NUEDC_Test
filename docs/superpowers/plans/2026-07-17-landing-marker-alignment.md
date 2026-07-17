# Landing Marker Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the visual landing endpoint and the terminal aircraft marker centered on the mission start cell.

**Architecture:** `HProblemView` passes the existing `start_cell` into `GridScene::setLandingTarget`. `GridScene` owns one cached visual landing position, uses it for the blue endpoint and corridor, and reuses it when `setCurrentCell("touchdown")` receives the terminal non-grid waypoint.

**Tech Stack:** C++17, Qt 6 Graphics View Framework, Qt Test, CMake/CTest.

## Global Constraints

- Actual touchdown coordinates, mission plans, and airborne execution waypoints must remain unchanged.
- Only the ground-station visualization may change.
- Unknown non-grid waypoint identifiers other than `touchdown` must continue to hide the current-position marker.
- Preserve the existing uncommitted heartbeat fix and unrelated untracked files.

---

### Task 1: Center landing and terminal aircraft markers on the start cell

**Files:**
- Modify: `ground_station_computer/tests/test_grid_scene.cpp`
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_grid_scene.cpp`
- Modify: `ground_station_computer/src/h_problem/ui/h_problem_view.cpp`

**Interfaces:**
- Consumes: `HProblemView::showRoute(..., const QString &start_cell, const QString &descent_start_cell, double touchdown_x_cm, double touchdown_y_cm, bool landing_enabled)`.
- Produces: `GridScene::setLandingTarget(const QString &start_cell, const QString &descent_start_cell, double touchdown_x_cm, double touchdown_y_cm, bool enabled)`.
- Produces: cached `std::optional<QPointF> landing_target_position_` and private `setCurrentMarkerPosition(const QPointF &position)`.

- [ ] **Step 1: Write failing blue-marker and corridor alignment tests**

Update calls to include the start cell and change the landing assertions to require its center even when the supplied touchdown coordinates are offset:

```cpp
scene.setLandingTarget("A9B1", "A8B3", 260.0, 190.0, true);

const QPointF expected_landing = testCellCenter("A9B1");
QCOMPARE(touchdown->sceneBoundingRect().center(), expected_landing);
QCOMPARE(corridor->line().p1(), testCellCenter("A8B3"));
QCOMPARE(corridor->line().p2(), expected_landing);
```

Rename the coordinate data test to `touchdownCoordinatesDoNotOffsetLandingMarker` and assert all data rows stay centered on `A9B1`:

```cpp
scene.setLandingTarget(
    "A9B1", "A8B3", touchdown_x_cm, touchdown_y_cm, true);
QCOMPARE(touchdown->sceneBoundingRect().center(), testCellCenter("A9B1"));
```

- [ ] **Step 2: Write failing terminal current-marker tests**

Replace `hidesCurrentMarkerForNonGridWaypoint` with separate terminal and invalid-identifier behavior:

```cpp
void GridSceneTests::showsCurrentMarkerAtLandingTargetForTouchdown() {
    TestableGridScene scene;
    scene.setLandingTarget("A9B1", "A8B3", 444.0, 333.0, true);
    scene.setCurrentCell("A8B3");

    scene.setCurrentCell(QStringLiteral("touchdown"));

    auto *marker = findCurrentMarker(scene);
    QVERIFY(marker != nullptr);
    QVERIFY(marker->isVisible());
    QCOMPARE(marker->sceneBoundingRect().center(), testCellCenter("A9B1"));
}

void GridSceneTests::hidesCurrentMarkerForUnknownNonGridWaypoint() {
    TestableGridScene scene;
    scene.setCurrentCell("A8B3");

    scene.setCurrentCell(QStringLiteral("unknown-terminal"));

    auto *marker = findCurrentMarker(scene);
    QVERIFY(marker != nullptr);
    QVERIFY(!marker->isVisible());
}
```

- [ ] **Step 3: Build and run the focused tests to verify RED**

Run:

```bash
cmake --build build --target test_grid_scene -j2
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_grid_scene
```

Expected: compilation fails because `setLandingTarget` has no start-cell overload, or the new geometry/visibility assertions fail against the old implementation.

- [ ] **Step 4: Add the start-cell visual landing contract**

Change the scene API and state in `h_grid_scene.h`:

```cpp
#include <optional>

void setLandingTarget(
    const QString &start_cell,
    const QString &descent_start_cell,
    double touchdown_x_cm,
    double touchdown_y_cm,
    bool enabled);

void setCurrentMarkerPosition(const QPointF &position);
std::optional<QPointF> landing_target_position_;
```

In `setLandingTarget`, clear `landing_target_position_` before validation, validate both cell identifiers through `GridMapper::tryToPoint`, cache `cellCenter(start_cell)`, and use the cached point for the blue marker and corridor endpoint. Keep the coordinate parameters in the interface because `HProblemView` still receives canonical plan metadata, but explicitly mark them unused:

```cpp
Q_UNUSED(touchdown_x_cm);
Q_UNUSED(touchdown_y_cm);
landing_target_position_.reset();
if (!enabled || !GridMapper::tryToPoint(start_cell).has_value()
    || !GridMapper::tryToPoint(descent_start_cell).has_value()) {
    return;
}
const QPointF descent_start = cellCenter(descent_start_cell);
const QPointF touchdown = cellCenter(start_cell);
landing_target_position_ = touchdown;
```

- [ ] **Step 5: Preserve the red marker at `touchdown`**

Extract current-marker creation and positioning from `setCurrentCell`:

```cpp
void GridScene::setCurrentMarkerPosition(const QPointF &position) {
    if (current_marker_ == nullptr) {
        current_marker_ = addEllipse(
            -9.0, -9.0, 18.0, 18.0,
            QPen(QColor(255, 255, 255, 240), 2.0),
            QBrush(QColor(231, 76, 60)));
        current_marker_->setData(
            kCurrentMarkerDataKey, QLatin1String(kCurrentMarkerDataValue));
        current_marker_->setZValue(5.0);
    }
    current_marker_->setPos(position);
    current_marker_->show();
}
```

Handle the terminal identifier before hiding invalid cells:

```cpp
if (!point.has_value()) {
    if (cell_code == QLatin1String("touchdown")
        && landing_target_position_.has_value()) {
        setCurrentMarkerPosition(landing_target_position_.value());
    } else if (current_marker_ != nullptr) {
        current_marker_->hide();
    }
    return;
}
setCurrentMarkerPosition(cellCenter(cell_code));
```

- [ ] **Step 6: Pass the start cell from the view**

Update `HProblemView::showRoute`:

```cpp
grid_scene_->setLandingTarget(
    start_cell,
    descent_start_cell,
    touchdown_x_cm,
    touchdown_y_cm,
    landing_enabled);
```

Update all direct test calls to the new signature. When landing display is disabled, pass empty start/descent cells and verify that a later `touchdown` does not show a stale cached position.

- [ ] **Step 7: Build and run focused tests to verify GREEN**

Run:

```bash
cmake --build build --target test_grid_scene test_h_mission_controller ground_station_app -j2
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_grid_scene
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_h_mission_controller
```

Expected: all cases pass with zero failures.

- [ ] **Step 8: Run affected regression suite**

Run:

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure \
  -R 'test_grid_scene|test_h_mission_controller|test_main_window|test_reliable_command_client|test_command_link_monitor'
```

Expected: 5/5 tests pass.

- [ ] **Step 9: Verify the rendered application**

Launch the rebuilt application against the simulator, load `runtime/active_mission_plan.json`, and capture the task view before and after terminal telemetry. Verify:

```text
blue landing endpoint center == A9B1 center
landing corridor endpoint == A9B1 center
red current marker center at touchdown == A9B1 center
ground_station_app remains alive
```

- [ ] **Step 10: Commit the implementation**

```bash
git add ground_station_computer/tests/test_grid_scene.cpp \
  ground_station_computer/src/h_problem/ui/h_grid_scene.h \
  ground_station_computer/src/h_problem/ui/h_grid_scene.cpp \
  ground_station_computer/src/h_problem/ui/h_problem_view.cpp
git commit -m "fix: align landing markers with takeoff cell"
```
