# 代码架构与目录重组设计方案

## 背景

当前仓库已经同时承载了：

- Qt6 地面站端
- Python 机载端 / 模拟端
- Protobuf 共享协议
- 静态案例文件
- 运行时任务计划文件

随着“手动设置禁飞区”“任务计划下发”“双机通信”逐步引入，当前根目录结构已经开始暴露出边界混乱的问题：

- `ground_station/` 和 `python/` 分别代表两端，但共享资源仍散落在根目录
- `cases/sample_case.json` 与 `cases/active_mission_plan.json` 混放，静态输入和运行时状态语义不清
- 构建脚本中仍硬编码了 `proto/`、`python/uav_testbed/`、`ground_station/` 等路径
- README 中的运行命令与目录命名耦合，后续进一步扩展双机通信时会持续恶化

本轮目标不是增加新业务能力，而是先把**目录边界、构建入口与运行时文件归属**整理清楚，为后续双机通信和机载命令服务打基础。

## 目标

将仓库重组为“**单仓库、双端分层、共享资源独立、运行态单独隔离**”的结构，并保证以下条件：

- 地面站端代码单独收拢
- 机载端代码单独收拢
- 共享协议与共享案例单独收拢
- 运行时任务计划文件不再混入静态案例目录
- 根目录仍保留单一构建入口
- 现有功能和测试在迁移后仍能通过

## 已确认方案

采用 **方案 1：`ground_control/ + airborne/ + shared/ + runtime/`**。

### 目标目录结构

```text
XT/
├─ ground_control/
│  ├─ CMakeLists.txt
│  ├─ src/
│  └─ tests/
├─ airborne/
│  ├─ tests/
│  └─ uav_testbed/
├─ shared/
│  ├─ cases/
│  └─ proto/
├─ runtime/
│  └─ active_mission_plan.json
├─ docs/
├─ CMakeLists.txt
├─ README.md
└─ .gitignore
```

## 文件迁移映射

### 地面站端

- `ground_station/` -> `ground_control/`
- `ground_station/src/*` -> `ground_control/src/*`
- `ground_station/tests/*` -> `ground_control/tests/*`
- `ground_station/CMakeLists.txt` -> `ground_control/CMakeLists.txt`

### 机载端

- `python/uav_testbed/` -> `airborne/uav_testbed/`
- `python/tests/` -> `airborne/tests/`

### 共享资源

- `proto/messages.proto` -> `shared/proto/messages.proto`
- `cases/sample_case.json` -> `shared/cases/sample_case.json`

### 运行时资源

- `cases/active_mission_plan.json` -> `runtime/active_mission_plan.json`

## 为什么单独拆出 `runtime/`

这是本轮重构最重要的边界修正之一。

当前 `cases/` 同时承载了：

- 比赛/联调输入样例
- 地面站实时生成的当前任务计划

这两类文件本质上不是同一类资源：

- `sample_case.json` 是**静态样例**
- `active_mission_plan.json` 是**运行时状态**

若继续混放，会导致：

- 误把运行中间态当成静态案例提交
- 地面站和机载端都在“案例目录”中读写状态文件，语义混乱
- README 与测试说明里难以区分“输入样例”和“当前执行计划”

因此本轮明确将运行态文件迁移到 `runtime/`。

## 构建设计

### 根目录 `CMakeLists.txt`

根目录继续保留为统一入口，职责如下：

- 查找 Qt6 / Protobuf / ZeroMQ 依赖
- 从 `shared/proto/messages.proto` 生成 C++ protobuf 代码
- 将 Python protobuf 代码生成到 `airborne/uav_testbed/generated/`
- 暴露共享的 `proto_messages` 库
- `add_subdirectory(ground_control)`

### `ground_control/CMakeLists.txt`

地面站子目录只负责 Qt 目标：

- `ground_control_app`
- 地面站单元测试目标

也就是说：

- 根目录负责共享依赖和协议生成
- 子目录负责本端应用与测试

这种分工最接近当前代码结构，改动最小。

## Python 运行设计

Python 端仍然不进入 CMake 编译，但运行入口统一更新为：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json
```

迁移后要统一修正的路径包括：

- `run_simulator.py` 默认 `--case`
- `run_simulator.py` 默认 `--mission-plan`
- `export_mission_plan.py` 默认 `--case`
- Python 测试中的案例路径
- `PYTHONPATH=python` -> `PYTHONPATH=airborne`

## 地面站运行设计

迁移后地面站二进制名称建议同步调整，避免路径名仍停留在旧结构：

- `ground_station_app` -> `ground_control_app`

最终启动命令应为：

```bash
./build/ground_control/ground_control_app
```

同时地面站内部默认路径需要同步更新：

- 当前样例案例路径 -> `shared/cases/sample_case.json`
- 当前任务计划输出路径 -> `runtime/active_mission_plan.json`
- 共享协议路径由构建系统负责，不在运行时硬编码

## README 重写范围

README 需要同步调整以下内容：

- 目录说明
- 构建命令
- Qt 测试命令
- Python 测试命令
- 地面站启动命令
- 模拟端启动命令
- 手动设置禁飞区后的任务计划输出路径

### 迁移后的推荐命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s airborne/tests -v
./build/ground_control/ground_control_app
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator --case shared/cases/sample_case.json
```

## 测试与回归策略

本轮重构不是业务逻辑改造，因此验收标准应聚焦在“路径迁移后功能不回归”。

### Qt 侧必须通过

- `test_detection_repository`
- `test_grid_mapper`
- `test_route_validator`
- `test_route_visualizer`
- `test_no_fly_zone_rules`
- `test_planning_state_machine`
- `test_mission_plan_bridge`
- `test_grid_scene`
- `test_main_window`

### Python 侧必须通过

- `airborne/tests/test_route_planner.py`
- `airborne/tests/test_simulator.py`
- `airborne/tests/test_mission_planning.py`

### 命令行回归

至少验证：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.export_mission_plan \
  --case shared/cases/sample_case.json \
  --no-fly-cells A1B2 A2B2 A3B2
```

以及：

```bash
./build/ground_control/ground_control_app
```

## 实施顺序

为降低风险，本轮迁移按以下顺序进行：

1. 迁移共享目录
   - `proto/` -> `shared/proto/`
   - `cases/sample_case.json` -> `shared/cases/sample_case.json`
   - `cases/active_mission_plan.json` -> `runtime/active_mission_plan.json`

2. 迁移机载端目录
   - `python/uav_testbed/` -> `airborne/uav_testbed/`
   - `python/tests/` -> `airborne/tests/`

3. 迁移地面站目录
   - `ground_station/` -> `ground_control/`

4. 修正根构建入口与子构建脚本

5. 修正 README 与 `.gitignore`

6. 跑全量回归验证

## 风险与约束

### 风险 1：路径硬编码遗漏

这是本轮最大风险。当前仓库中大量路径散落在：

- CMake
- README
- Python CLI 默认参数
- Qt 默认文件路径
- 测试
- 文档

因此迁移必须先完成“全文搜索 + 批量修正”，不能只搬文件。

### 风险 2：生成代码路径变化

`messages_pb2.py` 的生成路径迁移到 `airborne/uav_testbed/generated/` 后，需要确认：

- Python import 不变
- CMake 自定义命令输出路径更新正确
- `.gitignore` 仍忽略生成文件

### 风险 3：运行时路径与工作目录耦合

此前已经遇到过“从 `build/ground_station` 运行时路径解析失效”的问题。  
因此迁移后所有默认路径都必须尽量基于“仓库根目录解析”，而不是继续依赖工作目录碰巧正确。

## 不在本轮范围内

本轮不做以下内容：

- 双机命令链路实现
- 机载端 command server
- 地面站 command client
- 协议扩展（Ack / Command）
- 业务逻辑重写
- UI 视觉重做

## 结论

本轮应先把仓库整理为：

- `ground_control/`
- `airborne/`
- `shared/`
- `runtime/`

并同步完成根构建入口、Qt 子构建、Python 默认路径、README、`.gitignore` 的一致化修正。  
这样做能在不引入新业务复杂度的前提下，把地面站端、机载端与共享资源彻底分层，为下一阶段双机通信开发提供清晰边界。
