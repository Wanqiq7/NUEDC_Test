# 地面站双 NUC 联调操作手册

## 1. 目标

本文档说明当前地面站仓库如何连接外部机载端，完成任务下发、启动执行和遥测回传联调。

当前仓库只保留地面站端代码、共享协议/规划核心和运行态数据：

```text
NUEDC_Test/
├─ ground_station_computer/
├─ shared/
├─ runtime/
├─ docs/
├─ scripts/
├─ CMakeLists.txt
└─ README.md
```

机载端程序应在独立机载仓库或设备侧工程中维护。本仓库不再包含本地 `airborne_computer/` 或机载 ROS2 bridge 源码。

## 2. 地面站环境

建议地面站 NUC 满足：

- Ubuntu 20.04 / 22.04 / 24.04
- CMake 3.16 或更高
- g++ / clang++，支持 C++17
- Qt6（至少包含 `Core`、`Widgets`、`Sql`、`Test`）
- Protobuf 编译器与运行库
- ZeroMQ / cppzmq

安装示例：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  protobuf-compiler \
  libprotobuf-dev \
  libzmq3-dev \
  cppzmq-dev \
  qt6-base-dev \
  qt6-base-dev-tools
```

## 3. 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

以上命令会生成 C++ Protobuf 代码，并编译共享核心库与 Qt 地面站。

## 4. 地面站重点目录

- `ground_station_computer/`：Qt 地面站源码和测试。
- `shared/proto/`：与机载端约定的 Protobuf 协议。
- `shared/cpp/`：地面站使用的通用任务模型、H 题规划与协议编解码。
- `shared/cases/`：固定样例案例。
- `runtime/`：地面站生成的任务计划和运行数据库。
- `scripts/`：地面站网络配置与检查脚本。

## 5. 双机网络配置

推荐网络拓扑为地面站 NUC 提供 Wi-Fi 热点，机载 NUC 作为客户端自动连接。默认网段和地址为：

- 地面站热点：`10.42.0.1/24`
- 机载端：`10.42.0.2/24`

首次配置或更换 Wi-Fi 网卡后，先在地面站 NUC 执行：

```bash
./scripts/start_ground_hotspot.sh \
  --iface wlan0 --ssid NUEDC-Ground --password '12345678'
```

再在机载 NUC 执行：

```bash
cd /home/sb/Ground_station/point_lio_mid360_ros2
./src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh \
  --iface wlan0 --ssid NUEDC-Ground --password '12345678'
```

两份 NetworkManager 配置均开启了自动连接。此后两台设备上电后会自动恢复热点和客户端连接，无需重复运行脚本。`wlan0` 仅为示例；脚本会自动检测 Wi-Fi 网卡，也可用 `--iface` 指定实际接口。

地面站脚本会生成通信环境文件：

```bash
source runtime/ground_control_network.env
./scripts/check_ground_control_network.sh --host 10.42.0.2
```

端口约定：

- `5557`：机载端 `PUB`，地面站 `SUB`，用于遥测和任务事件。
- `5558`：机载端 `REP`，地面站 `REQ`，用于任务下发、PING、开始、停止和视觉控制命令。

`setup_ground_control_network.sh` 保持可用，但仅用于旧的反向拓扑，即机载端提供热点、地面站连接机载热点：

```bash
./scripts/setup_ground_control_network.sh --host 192.168.10.20
source runtime/ground_control_network.env
./scripts/check_ground_control_network.sh --host 192.168.10.20
```

## 6. 启动地面站

手动启动：

```bash
source runtime/ground_control_network.env
./build/ground_station_computer/ground_station_app
```

配置地面热点后直接启动：

```bash
./scripts/start_ground_hotspot.sh --launch-app
```

启动后，地面站会尝试 PING 外部机载端，并显示 `机载状态: 在线` 或 `机载状态: 离线`。

## 7. 任务下发流程

在地面站界面中：

1. 点击 `设置禁飞区`。
2. 在左侧地图选择 3 个横向或纵向连续格子。
3. 按钮变成 `航线生成`。
4. 点击 `航线生成`。

若外部机载端在线且命令链路正常，应看到：

- 地面站本地航线刷新。
- `runtime/active_mission_plan.json` 更新。
- 状态栏提示 `任务已同步至机载端，可执行任务`。
- `执行任务` 按钮可点击。

## 8. 执行、停止与视觉控制

- 点击 `执行任务`：地面站发送 `START_MISSION`。
- 点击 `停止任务`：地面站发送 `STOP_MISSION`。
- 在机载端在线且已确认加载当前任务后，点击 `视觉武装`：地面站发送 `ARM_TARGETING`。
- 在机载端在线且已确认加载当前任务后，点击 `视觉复位`：地面站发送 `RESET_TARGETING`。

外部机载端需要按 `shared/proto/messages.proto` 的协议返回确认。确认中的 `vision_armed` 字段会显示为“视觉瞄准: 已武装”或“未武装”；外部机载端也应持续通过遥测端口发布任务事件。

## 9. 故障检查

地面端显示机载离线时：

```bash
ping 192.168.10.20
nc -vz 192.168.10.20 5558
```

任务生成成功但同步失败时，检查：

- `NUEDC_AIRBORNE_HOST` 是否指向外部机载端真实 IP。
- 外部机载端命令端口是否监听 `5558`。
- 两端防火墙是否放行 `5557` 和 `5558`。

遥测没有回传时，检查：

- 外部机载端遥测端口是否监听或发布到 `5557`。
- `NUEDC_TELEMETRY_PORT` 是否正确。
- `NUEDC_AIRBORNE_HOST` 是否指向外部机载端真实 IP。
