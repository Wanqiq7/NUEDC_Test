# 地面站双 NUC 联调操作手册

## 1. 目标

本文档说明 Web 地面站如何连接外部机载端，完成任务下发、启动执行和遥测回传联调。
标准操作入口为 `http://10.42.0.1:8000`。

当前仓库只保留地面站端代码、共享协议/规划核心和运行态数据：

```text
NUEDC_Test/
├─ web_ground_station/
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
- Chromium
- Python 3.10、uv、Node.js、Corepack/pnpm
- Qt6 Core、Test（仅供共享 C++ 规划核心使用）
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

以上命令会生成 C++ Protobuf 代码，并编译共享核心库与无状态规划 CLI。
赛前还需预热并构建 Web 依赖：

```bash
cd web_ground_station
uv sync --frozen
cd frontend
corepack pnpm install --frozen-lockfile
corepack pnpm build
```

## 4. 地面站重点目录

- `web_ground_station/`：比赛使用的 Gateway、Web 主控、启动脚本和测试。
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
cd /home/sb/Ground_station/Mid360_add_groundstation
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

## 6. 日常联调启动

热点和客户端连接首次配置完成后，日常联调不需要重复修改 NetworkManager 配置。
先在地面站从两棵干净仓库创建唯一部署清单并做双端契约验证：

```bash
cd /path/to/NUEDC_Test
web_ground_station/scripts/verify_cross_repo_manifest_contract.sh \
  --ground-repo "$PWD" --airborne-repo /path/to/airborne-repo \
  --model /path/to/ani_rk3588_fp16.rknn \
  --output runtime/deployment_manifest.json
```

把该文件复制为机载 staging 文件并在同一文件系统用 `mv` 原子替换发布清单，禁止两端分别
生成。Ground 环境设 `NUEDC_DEPLOYMENT_MANIFEST=runtime/deployment_manifest.json`；Airborne
环境设为发布清单绝对路径。随后使用机载仓库的离线安装脚本生成版本目录和 `current` 软链，
验证发布后再启动固定的 systemd 入口；回滚只切换 `current` 到上一完整版本，不拼接文件。

先在机载端设置硬件门禁所需环境变量，并通过联调入口启动 ROS 2 硬件栈：

```bash
cd /home/sb/Ground_station/Mid360_add_groundstation
export NUEDC_STM32_GATE_FILE=/home/cat/evidence/stm32-gate.json
export NUEDC_MODEL_PATH=/home/cat/Mid360_add_groundstation/packages/models/ani_rk3588_fp16.rknn
export NUEDC_DEPLOYMENT_MANIFEST=/opt/nuedc/current/deployment_manifest.json
./src/nuedc_airborne/airborne_bringup/scripts/start_airborne_integration.sh
```

该入口先确认地面站 `10.42.0.1` 可达，再调用受控的
`start_airborne_hardware.sh`；模型和 STM32 证据门禁仍然生效。

机载服务启动后，在地面站端执行 Web 比赛入口：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
source runtime/web_ground_station.env
web_ground_station/scripts/check_web_ground_station.sh
web_ground_station/scripts/start_competition.sh
```

该入口要求机载 `10.42.0.2`、遥测端口 `5557` 和命令端口 `5558` 全部通过，且确认
静态前端、锁文件、runtime 目录和 C++ 规划器均可用。任一检查失败时不启动服务。
启动后在 Chromium 打开 `http://10.42.0.1:8000`。

仅调试 UI、不要求机载服务在线时，使用带热更新的开发入口：

```bash
web_ground_station/scripts/start_dev.sh
```

开发入口不得用于比赛。任何时刻只允许一个 Web Gateway 向 `10.42.0.2:5558`
发送控制命令。

### 命令链路保活与状态

地面站会每两秒自动发送一次 `PING`，无需用户操作即可持续确认 REQ/REP 命令链路。第一次或第二次未收到确认时，状态显示为 `链路确认中`；第三次连续失败时显示为离线。之后只要收到任意一次有效的命令 Ack（包括后续 `PING` 的 Ack），链路会自动恢复在线，无需重启地面站或机载端。

`刷新机载链路` 按钮会请求一次立即探测，可用于人工诊断；保持在线不需要点击该按钮。遥测数据仅反映遥测通道，不能用于建立或恢复命令链路健康状态。

## 7. 任务下发流程

H 题执行契约为 h_field_m_v1。TaskWaypoint 使用米，A9B1 格心为原点，
+X: B1 -> B7，+Y: A9 -> A1。执行序列为 takeoff -> navigate -> land；
terminal_waypoint_id=touchdown 表示最终落点，metadata_json.terminal_cell
表示最后巡查格。START 由机载 mission_coordinator 直接接受并运行速度闭环，
不再调用 /nuedc/execute_mission Action。

该契约必须原子部署：机载端最终系统测试通过前，不得单独部署这版地面站契约；
地面站与机载端的 LOAD 契约门禁和速度控制器必须在同一部署窗口上线。

在地面站界面中：

1. 在 Web 主控点击 `设置禁飞区`。
2. 在左侧地图选择 3 个横向或纵向连续格子。
3. 按钮变成 `航线生成`。
4. 点击 `航线生成`。

若外部机载端在线且命令链路正常，应看到：

- 地面站本地航线刷新。
- `runtime/active_mission_plan.json` 更新。
- 状态栏提示 `任务已同步至机载端，可执行任务`。
- `执行任务` 按钮可点击。

## 8. 执行、停止与视觉控制

- 点击 `START` 并确认：地面站发送 `START_MISSION`。
- 点击 `STOP` 并确认：地面站发送 `STOP_MISSION`。
- `START_MISSION` 成功后机载端自动开启视觉；地面站不再提供飞行中的手动视觉按钮。
- `ARM_TARGETING` 与 `RESET_TARGETING` 仍保留在线协议兼容能力，不属于正常一键流程。

每次启动地面站都会创建独立检测会话，不会加载上一次启动的记录。任务结束后可点击
`动物查询`，按动物品种查看本次会话的总数以及各 `A?B?` 方格内的数量。

外部机载端需要按 `shared/proto/messages.proto` 的协议返回确认。确认中的 `vision_armed` 字段会显示为“视觉瞄准: 已武装”或“未武装”；外部机载端也应持续通过遥测端口发布任务事件。

浏览器失效且 Web Gateway 已退出时，可独立发送一次应急 STOP：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
uv run python -m nuedc_web_gateway.emergency_stop \
  --task-id '<当前任务 ID>' --env-file ../runtime/web_ground_station.env
```

只有退出码为 0 且输出“紧急停止已确认”才表示收到机载 ACK。浏览器刷新或 Gateway 重启
不会自动 STOP；重启后等待状态从“同步中”恢复“在线”，再允许 START。

## 9. PlotJuggler PID 波形调试

Web 地面站不提供 PID 调试页。需要波形调参时，让机载端把既有 UDP JSON 发送到
`10.42.0.1:9870`，并在地面站 PlotJuggler 中选择 UDP Streaming/JSON 解析、监听
`9870`。Gateway 不绑定该端口。比赛任务执行不依赖 PlotJuggler，非调试时无需启动。

## 10. 故障检查

地面端显示机载离线时：

```bash
ping 10.42.0.2
nc -vz 10.42.0.2 5558
```

任务生成成功但同步失败时，检查：

- `NUEDC_AIRBORNE_HOST` 是否指向外部机载端真实 IP。
- 外部机载端命令端口是否监听 `5558`。
- 两端防火墙是否放行 `5557` 和 `5558`。

遥测没有回传时，检查：

- 外部机载端遥测端口是否监听或发布到 `5557`。
- `NUEDC_TELEMETRY_PORT` 是否正确。
- `NUEDC_AIRBORNE_HOST` 是否指向外部机载端真实 IP。
