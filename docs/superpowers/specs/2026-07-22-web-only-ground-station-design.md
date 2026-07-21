# Web-only 地面站与 Qt-free C++ 规划器设计

日期：2026-07-22

状态：设计已批准，等待书面评审

目标仓库：`NUEDC_Test`

设计基线：`feat/stack-optimization-ground` @ `66ce1da`

## 1. 决策摘要

地面站最终只保留 Web 实现。唯一允许存在的地面端原生模块，是一个无状态、无 Qt
依赖的 C++17 H 题路径规划 CLI：

> The ground station is Web-only; the sole permitted native module is a Qt-free
> C++17 stateless route-planning CLI.

Web UI、FastAPI Gateway、热点部署、ZeroMQ/Protobuf 机载通信、JSONL 记录和现有
比赛启动流程继续保留。路径规划算法不重写，Gateway 调用规划器的
`asyncio.to_thread(...)` + 同步 `subprocess.Popen` 模型不改变。

本次工作只移除 Qt 地面站遗留代码和依赖，并把规划 CLI 的传递依赖迁移到 STL、
`nlohmann_json` 和 GoogleTest。它不改变 UI、规划算法、地空协议或飞行控制行为。

## 2. 当前问题

当前 Web 分支已经删除 `ground_station_computer/`，但 `shared/cpp/` 仍不是独立的规划器：

- 顶层 CMake 强制查找 Qt6、C++ Protobuf 和 ZeroMQ，并启用 `CMAKE_AUTOMOC`。
- 规划算法和模型使用 `QString`、`QStringList`、`QVector`、`QSet`、`QMap`、`QPoint`
  等 Qt 类型。
- 案例加载、任务计划 JSON 和 CLI 请求/响应依赖 `QFile`、`QJson*` 等 Qt API。
- `competition_core` 同时包含规划器真正需要的任务计划模型，以及旧 Qt 地面站的命令
  处理、协议包装、文件存储和并发状态。
- `shared/cpp` 仍构建模拟器、运行时 fixture 工具及 QtTest 测试。

结果是 Web 地面站虽然没有 Qt UI，仍必须安装和链接 Qt 才能构建唯一需要的原生工具，
没有达到“只保留 Web 地面站”的目标。

## 3. 目标结构

完成后的地面站仓库结构为：

```text
NUEDC_Test/
├── CMakeLists.txt
├── shared/
│   ├── cases/                 # H 题案例和迁移黄金输出
│   ├── cpp/                   # 最小 Qt-free C++17 规划器、CLI、GoogleTest
│   └── proto/                 # Gateway 与机载端共享的权威协议源
├── web_ground_station/        # Vue 3 + FastAPI Gateway
├── runtime/                   # 比赛配置和运行时输出
├── scripts/                   # 热点与系统启动工具
├── docs/
└── AGENTS.md
```

以下内容不得存在于最终源码或构建依赖中：

- `ground_station_computer/` 或任何 Qt Widgets UI。
- Qt6、QtTest、AUTOMOC、`Q_OBJECT` 和 `#include <Q...>`。
- 仅为旧地面站服务的 C++ ZeroMQ/Protobuf 适配层。
- 旧 C++ 模拟器、命令状态机、任务计划磁盘存储和 runtime fixture 生成器。

历史设计文档可保留对 Qt 的叙述，因为它们是迁移记录，不参与源码、构建或运行。
当前运行生成物和缓存不作为源码保留边界的依据；由 `.gitignore` 与部署清单继续管理。

## 4. 保留与删除边界

### 4.1 必须保留并迁移

以下能力构成规划 CLI 的最小闭包：

- `h_problem_core/common/models`：案例、动物、降落剖面、任务时序等值对象。
- `mission/case_loader`：从 `shared/cases/*.json` 读取并验证案例。
- `mission/mission_planning`：把规划路线转换为 canonical `TaskPlan`。
- `planning/mission_geometry`：A/B 格网、厘米坐标、米制执行坐标、下降与触地点几何。
- `planning/route_cost`：任务时间和代价计算。
- `planning/route_planner`：权威路径搜索算法。
- `tools/planner_cli` 与 `h_route_planner_cli_main.cpp`：stdin/stdout 进程契约。
- 生成 canonical `TaskPlan` JSON 所需的最小任务计划值对象和纯内存 JSON 转换。

任务计划值对象从旧 `competition_core/task/models.h` 中拆出，只保留
`TaskPlan`/`TaskWaypoint` 及规划输出实际使用的字段。`CommandState`、`AckResult`、
`TaskEvent`、`TaskSummary` 和 Qt 并发元数据不属于规划器闭包，必须删除。依赖扫描若
发现规划器仍引用这些类型，必须先解除引用，不得扩大保留范围或因此保留 Qt。

### 4.2 必须删除

- `src/runtime/simulator.cpp` 及其测试。
- `generate_h_runtime_fixture` 工具。
- C++ `competition_command_handler` 和 `envelope_codec` 及其测试。
- 旧 `task_plan_store` 的文件读写职责及其测试。
- 与规划 CLI 无关的 `task_ports`、命令状态和旧 C++ JSON 文件存储。
- C++ Protobuf 生成目标、`proto_messages`、C++ ZeroMQ 查找和链接。
- 所有 QtTest 测试及 Qt 专用架构测试。

`shared/proto/messages.proto` 不删除。它仍是 Python Gateway 与机载端通信的唯一协议源，
由 `web_ground_station/scripts/generate_python_proto.py` 管理 Python 生成物。删除的只是
地面端 CMake 中不再被规划 CLI 使用的 C++ Protobuf 构建链。

### 4.3 最终依赖图

```text
Vue frontend
      |
      v
FastAPI Gateway -- pyzmq + Python Protobuf --> airborne
      |
      | thread worker + synchronous subprocess.Popen
      v
h_route_planner_cli
      |
      +-- C++17 STL
      +-- nlohmann_json
      +-- planner/model/case-loader sources

planner tests -- GoogleTest --> planner library
```

CLI 的生产目标不得链接 Qt、Protobuf、ZeroMQ、Python 或 ROS 2。

## 5. Qt 到标准 C++ 的迁移规则

迁移只替换表示与基础设施，不修改算法分支、搜索顺序或代价公式。

| 当前类型/API | 最终类型/API |
| --- | --- |
| `QString` | `std::string`，统一 UTF-8 |
| `QStringList` / `QVector<T>` | `std::vector<T>` |
| `QSet<T>` | `std::set<T>` |
| `QMap<K,V>` | `std::map<K,V>` |
| `QHash<K,V>` | `std::unordered_map<K,V>`，仅当遍历顺序不影响结果 |
| `QQueue<T>` | `std::queue<T>` |
| `QPoint` | 明确字段的规划器内部坐标结构 |
| `quint32` / `quint64` | `std::uint32_t` / `std::uint64_t` |
| `QFile` | `std::ifstream` |
| `QJson*` | `nlohmann::json` |
| `QtTest` | GoogleTest |

容器选择必须保护确定性。凡是容器迭代顺序可能影响候选节点、同成本路线或最终 JSON
数组顺序，优先使用有序容器或显式排序。不得仅为性能把 `QMap`/`QSet` 机械替换为无序
容器。

字符串错误信息允许保持英文，但已有 CLI `error_code`、退出码和测试依赖的稳定消息必须
保持。文件路径通过 `std::filesystem::path` 表达；案例文件按 UTF-8 JSON 读取。

## 6. JSON 与 CLI 兼容契约

规划器可执行文件路径保持：

```text
build/shared/cpp/h_route_planner_cli
```

`NUEDC_PLANNER_CLI` 的环境变量语义不变。CLI 每次启动只处理一个 stdin JSON 文档，
输出一个 stdout JSON 文档，然后退出；不持有跨请求状态，也不写 runtime 文件。

请求 schema 保持：

```json
{
  "schema": "h_planning_request_v1",
  "case_path": "shared/cases/sample_case.json",
  "no_fly_cells": ["A2B3", "A2B4", "A2B5"]
}
```

成功响应保持：

```json
{
  "ok": true,
  "plan": {},
  "metrics": {
    "estimated_mission_time_s": 0.0,
    "planning_optimality": "proven_optimal"
  }
}
```

失败响应保持：

```json
{
  "ok": false,
  "error_code": "invalid_request",
  "message": "..."
}
```

退出码固定为：

- `0`：成功。
- `2`：请求不是合法对象、schema/case_path 无效或 `no_fly_cells` 类型无效。
- `3`：输入结构合法，但禁飞区、案例加载或路径规划失败。
- `4`：规划器内部错误。

`stdout` 不得出现日志；诊断只写 `stderr`。JSON 对象键的文本顺序和浮点文本格式不作为
兼容契约，测试通过解析后的 JSON 值比较。以下内容属于严格兼容契约：

- `ok`、`error_code`、`message`、`plan` 和 `metrics` 的字段语义。
- canonical plan 的 `message_type`、任务 ID/type、起终点 ID 和 metadata。
- waypoint 数量、顺序、重复访问、ID、`sequence_index`、XYZ、action 和 payload。
- `estimated_mission_time_s`、`planning_optimality` 与 metadata 中的成本/规划字段。
- A9 到 A1、B1 到 B7 的坐标方向。
- 起点 `A9B1`、下降起点格和非格网真实 touchdown 的区别。
- `h_field_m_v1` 执行坐标契约。

`nlohmann_json` 必须拒绝 CLI 当前拒绝的结构错误。新增解析宽容性不得改变已有错误类别；
例如缺少 `no_fly_cells` 仍按无效请求处理。未知额外请求字段继续忽略，以保持当前行为。

## 7. 等价性保护与黄金结果

Qt 被删除后无法再直接运行旧实现，因此迁移必须先建立不可变基线：

1. 在当前 Qt 实现上构建 `h_route_planner_cli`。
2. 对 `shared/cases/sample_case.json` 和测试中覆盖的代表性禁飞布局生成成功输出。
3. 生成无效 JSON、错误 schema、空 case_path、错误 `no_fly_cells` 类型、越界格、案例
   不存在和不可规划布局的失败结果。
4. 将请求、标准化响应、退出码和必要的 `stderr` 约束存入 `shared/cases/golden/`。
5. 在 Qt 代码仍存在时提交黄金文件，使其来源可审计。

黄金比较分两层：

- CLI 层比较解析后的完整 JSON、退出码和 stdout 单文档约束。
- 算法层逐项比较路线、waypoint、重复访问、下降起点、touchdown、坐标、代价和
  optimality；浮点数使用现有算法测试所采用的明确容差，不使用字符串格式比较。

黄金数据只能由迁移前的基线生成。迁移过程中若新旧结果不同，默认视为回归；不得直接
更新黄金文件来让测试通过。确需改变规划行为时必须另立设计和提交，不属于本次工作。

## 8. 构建与测试设计

顶层 CMake 保持 C++17，只配置规划器和仓库级 shell 测试：

- 删除 `CMAKE_AUTOMOC`。
- 删除 `find_package(Qt6 ...)`。
- 删除 C++ Protobuf 代码生成与 `find_library(ZMQ...)`。
- 使用 `find_package(nlohmann_json CONFIG REQUIRED)`。
- 测试启用时使用 `find_package(GTest REQUIRED)`，并通过 `gtest_discover_tests` 注册。

开发验证命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

去 Qt 验收必须使用全新 build 目录，并执行源码和构建元数据扫描：

```bash
cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release
cmake --build build-noqt --parallel
ctest --test-dir build-noqt --output-on-failure
rg -n 'Qt6|QtTest|Q_OBJECT|#include <Q' CMakeLists.txt shared/cpp web_ground_station scripts
ldd build-noqt/shared/cpp/h_route_planner_cli
```

扫描命令预期无命中，`ldd` 不得显示 Qt、Protobuf 或 ZeroMQ。还必须运行：

```bash
cd web_ground_station
uv run pytest
uv run ruff check gateway tests scripts
cd frontend
pnpm test
pnpm typecheck
pnpm build
pnpm exec playwright test
```

比赛脚本测试和部署清单测试继续由顶层 `ctest` 或其现有独立命令覆盖。实机 RK3588
不是合并本次重构的硬门槛，因为规划结果与 Gateway 进程契约可在当前主机验证；实机
部署仍是赛前发布检查项，不得宣称本次测试替代了飞行验收。

## 9. 迁移顺序与提交边界

实施按可回退的小提交推进：

1. **基线提交**：新增黄金请求/响应和基线生成说明，不修改规划器。
2. **模型与算法提交**：用 STL 迁移最小模型、几何、成本和搜索，GoogleTest 锁定算法
   等价；CLI 仍可在该提交内完成编译和测试。
3. **JSON/CLI 提交**：迁移案例加载、任务计划序列化和 CLI 到 `nlohmann_json`，通过
   全部黄金测试。
4. **删除遗留提交**：删除模拟器、协议包装、状态机、文件存储、fixture 工具和对应
   QtTest。
5. **构建瘦身提交**：移除 Qt/C++ Protobuf/ZeroMQ CMake 依赖并完成 clean build 验收。
6. **文档提交**：更新仓库 README、仓库内 `AGENTS.md` 和工作区
   `/home/sb/Ground_station/AGENTS.md`，反映 Web-only 命令和边界。

每个提交必须独立构建或明确标记为与紧邻迁移提交不可分割；实施计划将进一步拆到具体
文件、测试命令和子代理任务。任何阶段出现行为差异，都可回退当前小提交，而不回退
已经验证的黄金基线。

开发继续使用独立 worktree，不在带有用户未提交修改的工作区根目录实施。子代理按任务
顺序工作，每个任务由独立审查者检查重要问题；共享工作树中不并行修改同一文件。

## 10. 文档更新

最终文档必须删除“构建 Qt 地面站”的现行指导，并明确：

- 地面站入口是 `web_ground_station/`，比赛地址为 `10.42.0.1:8000`。
- CMake 只构建无状态 C++ 规划器及其测试。
- Gateway 和前端分别使用 `uv` 与 `pnpm`。
- 修改规划算法时必须运行 C++/黄金/Gateway 规划器测试。
- 修改地空协议时编辑 `shared/proto/messages.proto`，不要编辑生成文件。
- `build/`、前端 `dist/`、虚拟环境、缓存、运行数据库和 session 日志是生成物。
- 规划器不得吸收通信、持久化、Web 状态或飞行状态机职责。

更新 `/home/sb/Ground_station/AGENTS.md` 时必须保留其中针对
`point_lio_mid360_ros2/` 的有效机载端说明，只替换已过时的 Qt 地面站段落。若该文件
存在用户未提交修改，实施者必须在当前内容上做定向编辑，不得覆盖或还原用户改动。

## 11. 非目标

- 不把 C++ 路径规划器改写为 Python、TypeScript 或 WebAssembly。
- 不调整路径搜索、代价函数、降落约束或坐标系。
- 不改变 Gateway 的 planner worker、超时、输出上限或原子落盘行为。
- 不改变 ZeroMQ/Protobuf 地空协议和 Python 生成流程。
- 不修改 Vue 页面布局、视觉设计或交互。
- 不新增 PID Web 页面；PID 波形继续交给 PlotJuggler。
- 不清理历史文档中对 Qt 的迁移记录。
- 不重构机载 ROS 2 工作区。

## 12. 验收标准

全部满足才视为完成：

1. 仓库中不存在 Qt UI 源码，生产源码/CMake 不含 Qt API 或依赖。
2. 仅保留规划 CLI 所需的 C++17 源码；旧模拟器、协议、状态机和存储已删除。
3. `build/shared/cpp/h_route_planner_cli` 路径、JSON schema、退出码和错误码兼容。
4. 所有黄金案例、算法测试和 Gateway planner 测试通过。
5. A9/B7 坐标、禁飞区、重复路径、下降起点和真实 touchdown 语义无变化。
6. clean build 的规划 CLI 不链接 Qt、Protobuf 或 ZeroMQ。
7. Gateway、前端、脚本和部署清单的现有测试通过。
8. Web 比赛入口、热点模式和禁止比赛热重载的行为不变。
9. 仓库 README、仓库 `AGENTS.md` 和工作区 `AGENTS.md` 与最终结构一致。
10. 最终变更经过一次全分支审查，Critical/Important 问题清零。
