# 新增题目 Adapter 指南

本仓库的默认题目是 H 题，但 Shell 和协议已经按通用任务框架收口。新增 G/D 等题目时，优先新增 Adapter，不复制主流程。

## 共享核心

- 通用模型、`TaskPlan` JSON 存储、Protobuf codec、控制命令处理放入 `competition_core`。
- 题目专有 case、规划、仿真、旧格式兼容放入独立 target，例如 `g_problem_core`。
- 新 target 可以依赖 `competition_core`，但 `competition_core` 禁止 include 任何题目目录。
- 对外传输统一使用 `competition::TaskPlan`、`competition::TaskEvent`、`competition::TaskSummary`。

## 地面站

- Shell 只保留在 `ground_station_computer/src/app/MainWindow`，不得 include `h_problem/*` 或新增题目目录。
- 新题目实现 `CompetitionTaskAdapter`，至少提供任务页面、Envelope/状态处理、任务下发、当前 task id 和初始预览加载。
- 题目 UI、数据库、规则校验、规划按钮状态全部放在题目 Adapter/Page 内。
- 新增测试放入 `ground_station_computer/tests/test_*.cpp`，覆盖初始预览、任务生成、mission load 和关键 UI 状态。

## 机载端

- 通用 seam 是 `airborne::MissionRuntime` 和 `airborne::EventPublisher`，只发布通用任务事件和汇总。
- 新题目新增 runtime，例如 `GProblemMissionRuntime`，内部可以复用题目仿真模型，但 public seam 不暴露题目消息类型。
- `CommandServer` 保持薄传输层，继续调用 `competition_core` 的命令处理。
- 新增测试放入 `airborne_computer/tests/test_*.cpp`，覆盖任务计划读取、start/stop 行为和事件转换。

## 提交前检查

```bash
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "h_problem_core|h_problem/" shared/cpp/include/competition_core shared/cpp/src/task shared/cpp/src/protocol
rg -n "h_problem/" ground_station_computer/src/app
rg -n "SimMessage" airborne_computer/src/airborne_runtime.h
```
