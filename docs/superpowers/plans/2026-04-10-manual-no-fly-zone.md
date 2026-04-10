# Manual No-Fly Zone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Qt 地面站新增“起飞前手动设置三格直线禁飞区”的完整闭环：单按钮进入设置、地图点击三格、合法性校验、调用 Python 重规划、刷新地图，并产出可供模拟端/无人机链路复用的任务配置。

**Architecture:** 复用现有 Python `plan_route(...)`，先把“案例 → 路线 → GridConfig”统一收敛到一个共享的 Python 规划入口，再由 Qt 通过 `QProcess` 调用该入口获取 JSON 结果。Qt 侧把“禁飞区形状校验”和“按钮状态机”拆成可单测的纯 C++ 辅助模块，`GridScene` 只负责候选格显示与点击回传，`MainWindow` 负责最终接线。

**Tech Stack:** C++17, Qt6 Widgets, QtTest, Python 3.10, unittest, JSON, QProcess, CMake

---

## File Structure

- `python/uav_testbed/mission_planning.py`
  - 新建，共享“案例 + 禁飞区覆盖 + 路径规划 + GridConfig 数据装配”逻辑。
- `python/uav_testbed/export_mission_plan.py`
  - 新建，命令行导出任务配置 JSON，供 Qt 用 `QProcess` 调用。
- `python/uav_testbed/simulator.py`
  - 修改，改为复用共享规划入口，避免生成逻辑分叉。
- `python/tests/test_mission_planning.py`
  - 新建，覆盖禁飞区覆盖、输出字段与路径一致性。
- `ground_station/src/no_fly_zone_rules.h`
  - 新建，纯函数校验“三格是否为横/竖连续直线”。
- `ground_station/src/planning_state_machine.h`
  - 新建，纯逻辑管理 `IdlePreview / SelectingNoFlyZone / ReadyToGenerate`。
- `ground_station/src/mission_plan_bridge.h`
  - 新建，封装 Python CLI 调用与 JSON 解析。
- `ground_station/src/grid_scene.h`
  - 修改，新增编辑模式、候选禁飞区与格子点击信号。
- `ground_station/src/grid_scene.cpp`
  - 修改，新增点击命中、候选高亮、正式禁飞区与候选态分层显示。
- `ground_station/src/main_window.h`
  - 修改，新增主按钮、状态字段、候选集合与规划桥接。
- `ground_station/src/main_window.cpp`
  - 修改，接线单按钮状态机、地图点击、合法性校验与规划结果应用。
- `ground_station/tests/test_no_fly_zone_rules.cpp`
  - 新建，校验横向/纵向通过、L 形/跳格失败。
- `ground_station/tests/test_planning_state_machine.cpp`
  - 新建，校验按钮文案与状态迁移。
- `ground_station/tests/test_mission_plan_bridge.cpp`
  - 新建，校验 JSON 解析与错误报告。
- `ground_station/tests/test_grid_scene.cpp`
  - 新建，校验候选态写入与编辑开关。
- `ground_station/CMakeLists.txt`
  - 修改，注册新辅助源文件与新增测试。
- `README.md`
  - 修改，补充“手动禁飞区生成任务”的联调说明。

### Task 1: 抽取共享 Python 任务规划入口

**Files:**
- Create: `python/uav_testbed/mission_planning.py`
- Create: `python/uav_testbed/export_mission_plan.py`
- Create: `python/tests/test_mission_planning.py`
- Modify: `python/uav_testbed/simulator.py`

- [ ] **Step 1: 先写失败测试，锁定共享规划入口输出**

```python
import pathlib
import sys
import unittest

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PYTHON_ROOT = PROJECT_ROOT / "python"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))

from uav_testbed.case_loader import load_case  # noqa: E402
from uav_testbed.mission_planning import build_mission_plan  # noqa: E402


class MissionPlanningTests(unittest.TestCase):
    def test_build_mission_plan_overrides_no_fly_cells_and_keeps_route_outside_forbidden_cells(self) -> None:
        case = load_case("cases/sample_case.json")
        plan = build_mission_plan(case, override_no_fly_cells=("A1B2", "A2B2", "A3B2"))

        self.assertEqual(plan["start_cell"], case.start_cell)
        self.assertEqual(plan["no_fly_cells"], ["A1B2", "A2B2", "A3B2"])
        self.assertEqual(plan["route"][0], case.start_cell)
        for blocked in plan["no_fly_cells"]:
            self.assertNotIn(blocked, plan["route"])
        self.assertIn("terminal_cell", plan)
        self.assertIn("landing_enabled", plan)

    def test_build_mission_plan_matches_simulator_config_shape(self) -> None:
        case = load_case("cases/sample_case.json")
        plan = build_mission_plan(case)

        self.assertEqual(plan["message_type"], "config")
        self.assertIn("case_id", plan)
        self.assertIn("route", plan)
        self.assertGreater(len(plan["route"]), 0)
```

- [ ] **Step 2: 运行 Python 单测，确认入口尚不存在时先失败**

Run: `python3 -m unittest python.tests.test_mission_planning -v`
Expected: FAIL，提示 `uav_testbed.mission_planning` 不存在或 `build_mission_plan` 未定义。

- [ ] **Step 3: 实现共享规划模块**

```python
from __future__ import annotations

from .models import CaseConfig
from .route_planner import plan_route


def build_mission_plan(
    case: CaseConfig,
    override_no_fly_cells: tuple[str, ...] | None = None,
) -> dict:
    no_fly_cells = tuple(override_no_fly_cells or case.no_fly_cells)
    route = plan_route(
        width=9,
        height=7,
        start_cell=case.start_cell,
        no_fly_cells=set(no_fly_cells),
        end_cell=case.start_cell if case.return_to_start else None,
        require_cycle=case.return_to_start,
        mission_mode="time_optimal_open" if case.landing is not None else "legacy",
        landing_profile=case.landing.to_profile() if case.landing is not None else None,
    )
    return {
        "message_type": "config",
        "case_id": case.case_id,
        "start_cell": case.start_cell,
        "no_fly_cells": list(no_fly_cells),
        "route": route,
        "terminal_cell": route[-1],
        "landing_enabled": case.landing is not None,
        "descent_angle_deg": case.landing.descent_angle_deg if case.landing else None,
        "takeoff_anchor_x_cm": case.landing.takeoff_anchor_cm.x_cm if case.landing else None,
        "takeoff_anchor_y_cm": case.landing.takeoff_anchor_cm.y_cm if case.landing else None,
    }
```

- [ ] **Step 4: 添加 CLI 导出入口，并让模拟器复用它**

```python
# export_mission_plan.py
from __future__ import annotations

import argparse
import json

from .case_loader import load_case
from .mission_planning import build_mission_plan


def main() -> int:
    parser = argparse.ArgumentParser(description="导出任务规划 JSON")
    parser.add_argument("--case", default="cases/sample_case.json")
    parser.add_argument("--no-fly-cells", nargs="*", default=None)
    args = parser.parse_args()

    case = load_case(args.case)
    plan = build_mission_plan(
        case,
        override_no_fly_cells=tuple(args.no_fly_cells) if args.no_fly_cells else None,
    )
    print(json.dumps(plan, ensure_ascii=False))
    return 0
```

```python
# simulator.py 中替换原始 route/config 生成逻辑
from .mission_planning import build_mission_plan

plan = build_mission_plan(case)
route = plan["route"]
yield plan
```

- [ ] **Step 5: 运行 Python 测试并验证 CLI 输出**

Run: `python3 -m unittest discover -s python/tests -v`
Expected: PASS

Run: `PYTHONPATH=python python3 -m uav_testbed.export_mission_plan --case cases/sample_case.json --no-fly-cells A1B2 A2B2 A3B2`
Expected: 输出一行 JSON，包含 `"no_fly_cells": ["A1B2", "A2B2", "A3B2"]` 与非空 `"route"`。

### Task 2: 新增禁飞区规则与按钮状态机纯逻辑

**Files:**
- Create: `ground_station/src/no_fly_zone_rules.h`
- Create: `ground_station/src/planning_state_machine.h`
- Create: `ground_station/tests/test_no_fly_zone_rules.cpp`
- Create: `ground_station/tests/test_planning_state_machine.cpp`
- Modify: `ground_station/CMakeLists.txt`

- [ ] **Step 1: 先写失败测试，锁定三格直线规则**

```cpp
#include <QtTest/QtTest>

#include "no_fly_zone_rules.h"

class NoFlyZoneRulesTests : public QObject {
    Q_OBJECT

private slots:
    void acceptsHorizontalTriple();
    void acceptsVerticalTriple();
    void rejectsLShapeTriple();
    void rejectsGapTriple();
};

void NoFlyZoneRulesTests::acceptsHorizontalTriple() {
    const auto result = NoFlyZoneRules::validateSelection(
        {"A1B2", "A2B2", "A3B2"},
        "A9B1");
    QVERIFY(result.is_valid);
}

void NoFlyZoneRulesTests::acceptsVerticalTriple() {
    const auto result = NoFlyZoneRules::validateSelection(
        {"A4B1", "A4B2", "A4B3"},
        "A9B1");
    QVERIFY(result.is_valid);
}

void NoFlyZoneRulesTests::rejectsLShapeTriple() {
    const auto result = NoFlyZoneRules::validateSelection(
        {"A1B1", "A2B1", "A2B2"},
        "A9B1");
    QVERIFY(!result.is_valid);
    QVERIFY(result.message.contains("横向或纵向连续"));
}

void NoFlyZoneRulesTests::rejectsGapTriple() {
    const auto result = NoFlyZoneRules::validateSelection(
        {"A1B3", "A3B3", "A2B3"},
        "A2B1");
    QVERIFY(result.is_valid);

    const auto gap = NoFlyZoneRules::validateSelection(
        {"A1B3", "A3B3", "A4B3"},
        "A2B1");
    QVERIFY(!gap.is_valid);
}
```

- [ ] **Step 2: 再写失败测试，锁定按钮状态迁移**

```cpp
#include <QtTest/QtTest>

#include "planning_state_machine.h"

class PlanningStateMachineTests : public QObject {
    Q_OBJECT

private slots:
    void entersSelectingAfterPrimaryClick();
    void switchesButtonTextWhenSelectionBecomesValid();
    void returnsToSetupLabelAfterSuccessfulGeneration();
};

void PlanningStateMachineTests::entersSelectingAfterPrimaryClick() {
    PlanningStateMachine machine;

    QCOMPARE(machine.primaryButtonText(), QString("设置禁飞区"));
    machine.handlePrimaryButtonClicked();
    QCOMPARE(machine.state(), PlanningUiState::SelectingNoFlyZone);
}

void PlanningStateMachineTests::switchesButtonTextWhenSelectionBecomesValid() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    machine.updateSelectionValidity(true);

    QCOMPARE(machine.state(), PlanningUiState::ReadyToGenerate);
    QCOMPARE(machine.primaryButtonText(), QString("航线生成"));
}
```

- [ ] **Step 3: 运行单测，确认辅助模块尚不存在时先失败**

Run: `cmake --build build --target test_no_fly_zone_rules test_planning_state_machine`
Expected: FAIL，提示头文件或符号不存在。

- [ ] **Step 4: 实现最小规则与状态机**

```cpp
struct NoFlyZoneValidationResult {
    bool is_valid = false;
    QString message;
};

class NoFlyZoneRules {
public:
    static NoFlyZoneValidationResult validateSelection(
        const QStringList &cells,
        const QString &start_cell);
};
```

```cpp
enum class PlanningUiState {
    IdlePreview,
    SelectingNoFlyZone,
    ReadyToGenerate,
};

class PlanningStateMachine {
public:
    PlanningUiState state() const;
    QString primaryButtonText() const;
    void handlePrimaryButtonClicked();
    void updateSelectionValidity(bool is_valid);
    void handleGenerationSucceeded();
};
```

- [ ] **Step 5: 注册测试并运行**

```cmake
list(APPEND GROUND_STATION_CORE_SOURCES
    src/no_fly_zone_rules.cpp
    src/planning_state_machine.cpp
    src/mission_plan_bridge.cpp
)

add_ground_station_test(test_no_fly_zone_rules tests/test_no_fly_zone_rules.cpp)
add_ground_station_test(test_planning_state_machine tests/test_planning_state_machine.cpp)
```

Run: `ctest --test-dir build --output-on-failure -R "test_no_fly_zone_rules|test_planning_state_machine"`
Expected: PASS

### Task 3: 实现 Qt 到 Python 的任务规划桥接

**Files:**
- Create: `ground_station/src/mission_plan_bridge.h`
- Create: `ground_station/src/mission_plan_bridge.cpp`
- Create: `ground_station/tests/test_mission_plan_bridge.cpp`
- Modify: `ground_station/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，先锁定 JSON 解析和错误报告**

```cpp
#include <QtTest/QtTest>

#include "mission_plan_bridge.h"

class MissionPlanBridgeTests : public QObject {
    Q_OBJECT

private slots:
    void parsesPlannerJsonPayload();
    void rejectsJsonWithoutRoute();
};

void MissionPlanBridgeTests::parsesPlannerJsonPayload() {
    const QByteArray payload = R"({
        "case_id": "wildlife-demo",
        "start_cell": "A9B1",
        "no_fly_cells": ["A1B2", "A2B2", "A3B2"],
        "route": ["A9B1", "A8B1", "A7B1"],
        "terminal_cell": "A7B1",
        "landing_enabled": true,
        "descent_angle_deg": 45.0,
        "takeoff_anchor_x_cm": 475.0,
        "takeoff_anchor_y_cm": 375.0
    })";

    const auto result = MissionPlanBridge::parsePlannerOutput(payload);
    QVERIFY(result.ok);
    QCOMPARE(result.plan.route.size(), 3);
    QCOMPARE(result.plan.no_fly_cells, QStringList({"A1B2", "A2B2", "A3B2"}));
}

void MissionPlanBridgeTests::rejectsJsonWithoutRoute() {
    const auto result = MissionPlanBridge::parsePlannerOutput(R"({"case_id":"demo"})");
    QVERIFY(!result.ok);
    QVERIFY(result.error_message.contains("route"));
}
```

- [ ] **Step 2: 实现桥接结构与 JSON 解析**

```cpp
struct MissionPlanData {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    QStringList route;
    QString terminal_cell;
    bool landing_enabled = false;
    double descent_angle_deg = 0.0;
    double takeoff_anchor_x_cm = 0.0;
    double takeoff_anchor_y_cm = 0.0;
};

struct MissionPlanResult {
    bool ok = false;
    MissionPlanData plan;
    QString error_message;
};

class MissionPlanBridge {
public:
    static MissionPlanResult parsePlannerOutput(const QByteArray &stdout_bytes);

    MissionPlanResult generatePlan(
        const QString &case_path,
        const QStringList &no_fly_cells) const;
};
```

- [ ] **Step 3: 用 `QProcess` 调用 Python CLI**

```cpp
MissionPlanResult MissionPlanBridge::generatePlan(
    const QString &case_path,
    const QStringList &no_fly_cells) const {
    QProcess process;
    process.setProgram("python3");
    process.setArguments({
        "-m", "uav_testbed.export_mission_plan",
        "--case", case_path,
        "--no-fly-cells",
    } + no_fly_cells);
    process.setProcessEnvironment([] {
        auto env = QProcessEnvironment::systemEnvironment();
        const QString python_path = QDir::current().filePath("python");
        env.insert("PYTHONPATH", python_path);
        return env;
    }());
    process.start();
    process.waitForFinished();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {.ok = false, .error_message = QString::fromUtf8(process.readAllStandardError())};
    }
    return parsePlannerOutput(process.readAllStandardOutput());
}
```

- [ ] **Step 4: 注册并运行桥接单测**

Run: `ctest --test-dir build --output-on-failure -R test_mission_plan_bridge`
Expected: PASS

- [ ] **Step 5: 手动验证桥接命令**

Run: `PYTHONPATH=python python3 -m uav_testbed.export_mission_plan --case cases/sample_case.json --no-fly-cells A1B2 A2B2 A3B2`
Expected: 返回可被 `MissionPlanBridge::parsePlannerOutput(...)` 成功解析的 JSON。

### Task 4: 为 GridScene 增加编辑模式、候选高亮与点击回传

**Files:**
- Modify: `ground_station/src/grid_scene.h`
- Modify: `ground_station/src/grid_scene.cpp`
- Create: `ground_station/tests/test_grid_scene.cpp`
- Modify: `ground_station/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，锁定候选态和编辑开关**

```cpp
#include <QtTest/QtTest>

#include "grid_scene.h"

class GridSceneTests : public QObject {
    Q_OBJECT

private slots:
    void storesCandidateCellsSeparatelyFromCommittedNoFlyCells();
    void ignoresClicksWhenEditModeDisabled();
};

void GridSceneTests::storesCandidateCellsSeparatelyFromCommittedNoFlyCells() {
    GridScene scene;
    scene.setNoFlyCells({"A4B3", "A5B3", "A6B3"});
    scene.setCandidateNoFlyCells({"A1B2", "A2B2"});

    QCOMPARE(scene.noFlyCells(), QStringList({"A4B3", "A5B3", "A6B3"}));
    QCOMPARE(scene.candidateNoFlyCells(), QStringList({"A1B2", "A2B2"}));
}
```

- [ ] **Step 2: 先把 `GridScene` 暴露成可测试接口**

```cpp
class GridScene : public QGraphicsScene {
    Q_OBJECT

public:
    void setNoFlyEditEnabled(bool enabled);
    void setCandidateNoFlyCells(const QStringList &cells);
    void clearCandidateNoFlyCells();
    QStringList noFlyCells() const;
    QStringList candidateNoFlyCells() const;

signals:
    void cellClicked(QString cell_code);
};
```

- [ ] **Step 3: 实现点击命中与候选态显示**

```cpp
// initializeGrid() 内：
// 1. 每个 cell rect 设置 data(0, cell_code)
// 2. 打开编辑模式时允许 mousePressEvent 命中格子
// 3. 命中后 emit cellClicked(cell_code)

// resetCellBrushes() 内：
// - 正式禁飞区：灰色
// - 候选禁飞区：蓝灰或高亮描边
// - 其他格子：默认底色
```

- [ ] **Step 4: 注册 widget 测试**

```cmake
add_executable(test_grid_scene
    tests/test_grid_scene.cpp
    src/grid_scene.cpp
    ${GROUND_STATION_CORE_SOURCES}
)
target_include_directories(test_grid_scene PRIVATE ${GROUND_STATION_INCLUDE_DIR})
target_link_libraries(test_grid_scene PRIVATE Qt6::Core Qt6::Widgets Qt6::Test Qt6::Sql protobuf::libprotobuf)
add_test(NAME test_grid_scene COMMAND test_grid_scene)
set_tests_properties(test_grid_scene PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: 运行测试确认候选态与正式态不互相覆盖**

Run: `ctest --test-dir build --output-on-failure -R test_grid_scene`
Expected: PASS

### Task 5: 在 MainWindow 接入单按钮状态机与规划结果应用

**Files:**
- Modify: `ground_station/src/main_window.h`
- Modify: `ground_station/src/main_window.cpp`
- Modify: `README.md`

- [ ] **Step 1: 扩展主窗口字段，保存任务上下文**

```cpp
QPushButton *planning_button_ = nullptr;
PlanningStateMachine planning_state_;
MissionPlanBridge mission_plan_bridge_;
QString case_file_path_ = "cases/sample_case.json";
QString current_case_id_;
QString current_start_cell_;
QString current_terminal_cell_;
QStringList candidate_no_fly_cells_;
QStringList committed_no_fly_cells_;
bool current_landing_enabled_ = false;
double current_descent_angle_deg_ = 0.0;
double current_takeoff_anchor_x_cm_ = 0.0;
double current_takeoff_anchor_y_cm_ = 0.0;
```

- [ ] **Step 2: 加入按钮并完成状态接线**

```cpp
planning_button_ = new QPushButton("设置禁飞区", this);
connect(planning_button_, &QPushButton::clicked, this, [this]() {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        planning_state_.handlePrimaryButtonClicked();
        candidate_no_fly_cells_.clear();
        grid_scene_->clearCandidateNoFlyCells();
        grid_scene_->setNoFlyEditEnabled(true);
        status_label_->setText("状态: 请选择 3 个横向或纵向连续的禁飞格");
        planning_button_->setText(planning_state_.primaryButtonText());
        return;
    }

    if (planning_state_.state() == PlanningUiState::ReadyToGenerate) {
        generateMissionPlanFromCandidateSelection();
    }
});
```

- [ ] **Step 3: 响应地图点击并驱动状态机**

```cpp
connect(grid_scene_, &GridScene::cellClicked, this, [this](const QString &cell_code) {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        return;
    }

    if (candidate_no_fly_cells_.contains(cell_code)) {
        candidate_no_fly_cells_.removeAll(cell_code);
    } else if (candidate_no_fly_cells_.size() < 3) {
        candidate_no_fly_cells_.append(cell_code);
    }

    grid_scene_->setCandidateNoFlyCells(candidate_no_fly_cells_);
    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    planning_state_.updateSelectionValidity(validation.is_valid);
    planning_button_->setText(planning_state_.primaryButtonText());
    status_label_->setText(validation.message.isEmpty() ? QString("状态: 已选择 %1/3 个禁飞格").arg(candidate_no_fly_cells_.size()) : QString("状态: %1").arg(validation.message));
});
```

- [ ] **Step 4: 实现“航线生成”按钮逻辑**

```cpp
void MainWindow::generateMissionPlanFromCandidateSelection() {
    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, candidate_no_fly_cells_);
    if (!result.ok) {
        status_label_->setText(QString("错误: %1").arg(result.error_message));
        return;
    }

    committed_no_fly_cells_ = result.plan.no_fly_cells;
    current_case_id_ = result.plan.case_id;
    current_start_cell_ = result.plan.start_cell;
    current_terminal_cell_ = result.plan.terminal_cell;
    current_landing_enabled_ = result.plan.landing_enabled;
    current_descent_angle_deg_ = result.plan.descent_angle_deg;
    current_takeoff_anchor_x_cm_ = result.plan.takeoff_anchor_x_cm;
    current_takeoff_anchor_y_cm_ = result.plan.takeoff_anchor_y_cm;

    grid_scene_->setNoFlyCells(committed_no_fly_cells_);
    grid_scene_->clearCandidateNoFlyCells();
    grid_scene_->setNoFlyEditEnabled(false);
    grid_scene_->setRoute(result.plan.route);
    grid_scene_->setStartCell(current_start_cell_);
    grid_scene_->setLandingTarget(
        current_terminal_cell_,
        current_takeoff_anchor_x_cm_,
        current_takeoff_anchor_y_cm_,
        current_landing_enabled_);

    planning_state_.handleGenerationSucceeded();
    planning_button_->setText(planning_state_.primaryButtonText());
    status_label_->setText("状态: 航线生成成功，可执行任务");
}
```

- [ ] **Step 5: 运行回归测试并补充 README 操作步骤**

Run: `cmake --build build`
Expected: PASS

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS

Run: `PYTHONPATH=python python3 -m uav_testbed.export_mission_plan --case cases/sample_case.json --no-fly-cells A1B2 A2B2 A3B2`
Expected: PASS

在 `README.md` 增加如下流程说明：

```markdown
### 手动设置禁飞区

1. 启动地面站。
2. 点击 `设置禁飞区`。
3. 在左侧地图上点击 3 个横向或纵向连续格子。
4. 按钮切换为 `航线生成` 后点击生成。
5. 地图刷新新的禁飞区与航线。
```

## Self-Review

- **Spec coverage:** 已覆盖单按钮交互、三格直线规则、显式状态机、地图候选态、Qt→Python 重规划桥接、生成后刷新与测试挂接。
- **Placeholder scan:** 计划内无 `TODO`、`TBD`、`implement later` 等占位语句；每个任务都给出明确文件、命令和最小代码骨架。
- **Type consistency:** 全文统一使用 `PlanningUiState`、`PlanningStateMachine`、`NoFlyZoneRules`、`MissionPlanBridge`、`build_mission_plan(...)` 这组名称，避免后续实现阶段漂移。
