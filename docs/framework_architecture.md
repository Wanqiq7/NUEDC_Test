# 无人机电赛通用框架架构护栏

本文档定义仓库的长期边界。新增题目、通信能力、规划算法或地面站 UI 时，优先按这里判断代码应该落在哪一层。

## 分层职责

- `competition_core`：唯一通用内核，只放通用 `TaskPlan`、`TaskEvent`、`TaskSummary`、协议编解码、命令处理、任务端口和通用校验。
- `<task>_problem_core`：题目专有算法、案例解析、仿真、任务元数据转换；可以依赖 `competition_core`，反向依赖禁止。
- `ground_station_computer/src/framework`：地面站 Shell、通信、配置、运行状态和题目 Adapter 端口，不写题目规则。
- `ground_station_computer/src/<task>`：题目页面、题目状态解释、题目规划入口、题目数据存储。
- `airborne_computer/src`：机载 Shell、通用 `MissionRuntime` 和命令服务；题目执行逻辑放入独立 runtime。
- `airborne_computer/ros2/nuedc_bridge`：真实机载 ROS2 桥接，只做 ROS2 消息与通用任务协议转换。

## 通用端口

- `TaskPlanner`：输入题目约束，输出通用 `TaskPlan` 或失败原因。
- `TaskCodec`：把题目私有字段收敛进 `metadata_json` / `payload_json`。
- `CompetitionTaskAdapter`：地面站题目页面与 Shell 的唯一交互边界。
- `MissionRuntime`：机载端执行任务或桥接真实无人机运行栈。
- `availableCompetitionTaskAdapters()` / `createConfiguredCompetitionTaskAdapter()`：题目 Adapter 注册和选择入口。

## 禁止事项

- `MainWindow` 禁止 include `h_problem` 或任何题目目录头文件。
- `competition_core` 禁止 include `<task>_problem_core`。
- 新题目禁止复制 `MainWindow`、通信客户端或命令服务。
- ROS2 bridge 禁止解析题目 UI 状态，只处理通用任务协议与 ROS2 topic。

## 新题目最小接入清单

1. 新增 `<task>_problem_core`，实现题目案例模型、规划器和 `TaskPlan` 转换。
2. 新增地面站 `<task>` Adapter，实现页面、规划、遥测/检测/汇总消费。
3. 在 `availableCompetitionTaskAdapters()` 中注册新 Adapter，并支持 `NUEDC_TASK_ADAPTER=<adapter_id>` 选择。
4. 新增机载 `<task>_mission_runtime` 或 ROS2 topic 转换。
5. 补测试：核心规划、Adapter 状态、工厂选择、任务下发、机载命令、边界依赖。

## 回归门禁

提交前至少运行：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

若修改通信链路，必须额外覆盖超时、重试、离线和重复任务下发。