# 通用无人机竞赛任务框架

这是一个面向无人机电赛题目的双端 C++ 联调工程。当前默认题目 Adapter 仍是
2025 年电赛 H 题野生动物巡查系统，但共享协议和运行链路已经抽成通用任务
模型：

- C++ 机载端
- Qt6 桌面地面站
- ZeroMQ + Protobuf 通用 `TaskPlan` / `TaskEvent` / `TaskSummary` 通信

## 目录

- `shared/proto/`：Protobuf 通用任务协议
- `shared/cases/`：固定测试案例
- `runtime/`：运行时任务计划
- `shared/cpp/`：`competition_core` 通用核心与 `h_problem_core` 默认 H 题 Adapter 核心
- `airborne_computer/`：C++ 机载端 Shell、通用运行接口与默认 H runtime
- `ground_station_computer/`：Qt 地面站 Shell、通用 Adapter 接口与默认 H 页面

## 构建

```bash
cmake -S . -B build
cmake --build build
```

以上构建会生成 C++ protobuf 代码到 `build/generated/proto`，并编译共享核心库、机载端和地面站。

## 运行测试

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## 启动联调

先启动机载端：

```bash
./build/airborne_computer/airborne_app --case shared/cases/sample_case.json
```

再启动地面站：

```bash
./build/ground_station_computer/ground_station_app
```

地面站应看到：

- 禁飞区显示为灰色
- 航线覆盖非禁飞区
- 当前巡查方格实时移动
- 动物检测结果滚动追加
- 汇总统计与数据库一致

### 手动设置禁飞区

1. 启动地面站。
2. 点击 `设置禁飞区`。
3. 在左侧地图上点击 3 个横向或纵向连续的格子。
4. 按钮变为 `航线生成` 后点击以生成航线。
5. 地图刷新新的禁飞区与航线。
6. 后续启动机载端时，会优先读取 `runtime/active_mission_plan.json` 中的最新任务计划。

### 双 NUC 最小联调

在机载端启动 C++ 机载程序：

```bash
./build/airborne_computer/airborne_app --case shared/cases/sample_case.json --mission-plan runtime/active_mission_plan.json --endpoint tcp://0.0.0.0:5557 --command-endpoint tcp://0.0.0.0:5558 --sleep-scale 0.02
```

地面站在本地生成航线后，会先更新 `runtime/active_mission_plan.json`，再通过通用 `mission_load(TaskPlan)` 向机载端命令端口下发任务计划。

### 双 NUC 地址配置

地面站端通过环境变量指定机载 NUC 地址：

```bash
export NUEDC_AIRBORNE_HOST=192.168.10.20
export NUEDC_TELEMETRY_PORT=5557
export NUEDC_COMMAND_PORT=5558
./build/ground_station_computer/ground_station_app
```

机载端建议绑定到所有网卡：

```bash
./build/airborne_computer/airborne_app \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --endpoint tcp://0.0.0.0:5557 \
  --command-endpoint tcp://0.0.0.0:5558 \
  --sleep-scale 0.02
```

当前已支持的控制命令能力：

- `mission_load`：地面站下发通用 `TaskPlan` 任务计划
- `COMMAND_TYPE_PING`：机载端返回 `pong`
- `COMMAND_TYPE_START_MISSION`：机载端记录开始执行请求
- `COMMAND_TYPE_STOP_MISSION`：机载端记录停止请求

新增题目不需要复制主流程。先扩展 `shared/proto/messages.proto` 中已有通用字段能表达的元数据，
再新增地面站 `CompetitionTaskAdapter` 实现和机载端 `MissionRuntime` 实现；禁止把题目专有 UI 或解析逻辑直接写回 Shell。详细步骤见 `docs/adding_task_adapter.md`。

若希望机载模拟端等待启动命令后再执行，可使用：

```bash
./build/airborne_computer/airborne_app \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --endpoint tcp://0.0.0.0:5557 \
  --command-endpoint tcp://0.0.0.0:5558 \
  --wait-for-start \
  --sleep-scale 0.02
```
