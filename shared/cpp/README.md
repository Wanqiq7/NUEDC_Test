# 通用任务共享 C++ 核心库

`shared/cpp` 按职责分层存放两端共享代码。当前有两个 CMake 目标：

- `competition_core`：通用任务模型、`TaskPlan` JSON 存储、Protobuf codec 与控制命令处理。
- `h_problem_core`：H 题案例解析、航线规划、仿真和 H 题到通用任务模型的 Adapter，依赖 `competition_core`。

运行时任务计划以 `competition::TaskPlan` 作为唯一规范模型；各题目核心可保留内部规划结构，但持久化和协议传输必须通过通用模型。

- `include/competition_core/task/`：通用任务模型，例如 `TaskDefinition`、`TaskPlan`、`TaskEvent`、`TaskSummary`。
- `include/competition_core/mission/`：通用 `TaskPlan` JSON 读写与校验。
- `include/competition_core/protocol/`：通用 `Envelope` codec 与 `MissionLoad` / 控制命令处理。
- `common/`：H 题案例数据模型，例如 `CaseConfig`、降落与时间配置。
- `planning/`：航线规划、方格几何、路线代价和降落终端区域计算。
- `mission/`：案例 JSON 解析与直接生成通用 `TaskPlan`。
- `protocol/`：通用 `TaskPlan` 协议与事件 payload 构造。
- `runtime/`：按 H 题任务路线生成通用任务事件。

新增跨题共享逻辑优先放入 `include/competition_core/`；只有 H 题专有兼容逻辑才继续放入 `include/h_problem_core/` 和 `src/<layer>/`。
