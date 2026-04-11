# H 题混合联调测试平台

这是一个用于验证 2025 年电赛 H 题主链路的混合方案工程：

- Python 机载模拟端
- Qt6 桌面地面站
- ZeroMQ + Protobuf 通信

## 目录

- `shared/proto/`：Protobuf 共享协议
- `shared/cases/`：固定测试案例
- `runtime/`：运行时任务计划
- `airborne/`：机载模拟端
- `ground_control/`：Qt 地面站

## 构建

```bash
cmake -S . -B build
cmake --build build
```

以上构建会同时生成：

- C++ protobuf 代码到 `build/generated/proto`
- Python protobuf 代码到 `airborne/uav_testbed/generated`

## 运行 Python 测试

```bash
python3 -m unittest discover -s airborne/tests -v
```

## 运行 Qt 测试

```bash
ctest --test-dir build --output-on-failure
```

## 启动联调

先启动地面站：

```bash
./build/ground_control/ground_control_app
```

再启动模拟端：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator --case shared/cases/sample_case.json
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
6. 后续启动模拟端时，会优先读取 `runtime/active_mission_plan.json` 中的最新任务计划。

### 双 NUC 最小联调

先在机载端启动任务接收服务：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.command_server --endpoint tcp://0.0.0.0:5558 --output runtime/active_mission_plan.json
```

再在机载端或本机启动模拟端遥测：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator --case shared/cases/sample_case.json --mission-plan runtime/active_mission_plan.json --endpoint tcp://0.0.0.0:5557 --command-endpoint tcp://0.0.0.0:5558 --sleep-scale 0.02
```

地面站在本地生成航线后，会先更新 `runtime/active_mission_plan.json`，再尝试向机载端的命令端口下发任务计划。

### 双 NUC 地址配置

地面站端通过环境变量指定机载 NUC 地址：

```bash
export NUEDC_AIRBORNE_HOST=192.168.10.20
export NUEDC_TELEMETRY_PORT=5557
export NUEDC_COMMAND_PORT=5558
./build/ground_control/ground_control_app
```

机载端建议绑定到所有网卡：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --endpoint tcp://0.0.0.0:5557 \
  --command-endpoint tcp://0.0.0.0:5558 \
  --sleep-scale 0.02
```

当前已支持的控制命令能力：

- `mission_load`：地面站下发任务计划
- `COMMAND_TYPE_PING`：机载端返回 `pong`
- `COMMAND_TYPE_START_MISSION`：机载端记录开始执行请求
- `COMMAND_TYPE_STOP_MISSION`：机载端记录停止请求

若希望机载模拟端等待启动命令后再执行，可使用：

```bash
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --endpoint tcp://0.0.0.0:5557 \
  --command-endpoint tcp://0.0.0.0:5558 \
  --wait-for-start \
  --sleep-scale 0.02
```
