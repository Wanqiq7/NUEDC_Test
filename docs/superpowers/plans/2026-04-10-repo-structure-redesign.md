# Repository Structure Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将仓库重组为 `ground_control/`、`airborne/`、`shared/`、`runtime/` 四层结构，同时修正根构建入口、Qt 子构建、Python 默认路径、README 与忽略规则，保证现有功能与测试不回归。

**Architecture:** 共享协议与共享案例移动到 `shared/`，运行态任务计划移动到 `runtime/`；Qt 地面站整体迁移到 `ground_control/`，Python 机载端与测试整体迁移到 `airborne/`。根 `CMakeLists.txt` 继续负责 Protobuf 生成与公共依赖，`ground_control/CMakeLists.txt` 负责地面站目标与测试。

**Tech Stack:** C++17, Qt6 Widgets, QtTest, Python 3.10, unittest, Protobuf, CMake

---

### Task 1: 迁移共享目录并修正基础路径

**Files:**
- Create: `shared/`
- Create: `runtime/`
- Move: `proto/messages.proto` -> `shared/proto/messages.proto`
- Move: `cases/sample_case.json` -> `shared/cases/sample_case.json`
- Move: `cases/active_mission_plan.json` -> `runtime/active_mission_plan.json`
- Modify: `.gitignore`

- [ ] **Step 1: 创建目标目录结构**
- [ ] **Step 2: 迁移协议与案例文件**
- [ ] **Step 3: 修正 `.gitignore` 中对样例和运行时文件的路径**
- [ ] **Step 4: 运行全文搜索，确认不再有旧 `proto/` 与 `cases/active_mission_plan.json` 的根目录假设残留**

### Task 2: 迁移机载端目录并修正 Python 路径

**Files:**
- Move: `python/uav_testbed/` -> `airborne/uav_testbed/`
- Move: `python/tests/` -> `airborne/tests/`
- Modify: `airborne/uav_testbed/run_simulator.py`
- Modify: `airborne/uav_testbed/export_mission_plan.py`
- Modify: `airborne/tests/*.py`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 迁移 Python 包与测试目录**
- [ ] **Step 2: 修正 `run_simulator.py` 的默认 `--case` / `--mission-plan` 路径**
- [ ] **Step 3: 修正 `export_mission_plan.py` 和 Python 测试中的仓库路径**
- [ ] **Step 4: 将 protobuf Python 输出路径改到 `airborne/uav_testbed/generated/`**

### Task 3: 迁移地面站目录并修正 Qt 构建

**Files:**
- Move: `ground_station/` -> `ground_control/`
- Modify: `ground_control/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `ground_control/src/main_window.cpp`
- Modify: `ground_control/src/main_window.h`
- Modify: `ground_control/tests/test_main_window.cpp`

- [ ] **Step 1: 迁移地面站目录**
- [ ] **Step 2: 将根 `CMakeLists.txt` 的 `add_subdirectory` 指向 `ground_control`**
- [ ] **Step 3: 如有必要，将可执行目标名从 `ground_station_app` 调整为 `ground_control_app`**
- [ ] **Step 4: 修正地面站代码中默认案例路径和运行态任务计划路径**
- [ ] **Step 5: 修正测试中引用的旧路径**

### Task 4: 重写 README 与命令入口

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`（如需，仅路径说明）

- [ ] **Step 1: 更新目录说明**
- [ ] **Step 2: 更新 Qt / Python 测试命令**
- [ ] **Step 3: 更新地面站与模拟端启动命令**
- [ ] **Step 4: 更新手动设置禁飞区后的任务计划输出路径说明**

### Task 5: 全量回归验证

**Files:**
- Verify only

- [ ] **Step 1: 运行 `cmake -S . -B build`**
- [ ] **Step 2: 运行 `cmake --build build`**
- [ ] **Step 3: 运行 `ctest --test-dir build --output-on-failure`**
- [ ] **Step 4: 运行 `python3 -m unittest discover -s airborne/tests -v`**
- [ ] **Step 5: 验证 `PYTHONPATH=airborne python3 -m uav_testbed.export_mission_plan --case shared/cases/sample_case.json --no-fly-cells A1B2 A2B2 A3B2`**

## Self-Review

- **Spec coverage:** 覆盖了共享目录迁移、机载端迁移、地面站迁移、构建入口修正、README 重写和全量回归验证。
- **Placeholder scan:** 无 `TODO`/`TBD` 占位符。
- **Type consistency:** 目录命名统一为 `ground_control`、`airborne`、`shared`、`runtime`；运行时计划路径统一为 `runtime/active_mission_plan.json`。
