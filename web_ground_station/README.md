# Web 地面站操作手册

Web 地面站是比赛模式的主地面站。FastAPI Gateway 调用现有 C++ 无状态规划器，使用
ZeroMQ/Protobuf 与机载端通信，再通过同源 HTTP/WebSocket 向 Vue 3 主控台提供
`nuedc.web.v1` JSON。标准比赛入口为 `http://10.42.0.1:8000`，最低完整操作分辨率为
1024x600。

Qt 地面站保留为迁移期回退实现。任何时刻只能运行一个控制客户端；禁止同时启动 Qt 和
Web 地面站向 `10.42.0.2:5558` 发送 LOAD、START、STOP 或 PING。

## 开发环境

先完成根工程构建，确保 C++ 规划器存在：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cd web_ground_station
uv sync --frozen
cd frontend
corepack pnpm install --frozen-lockfile
```

开发模式启用 Gateway 和 Vite 热更新，只用于本机联调：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
web_ground_station/scripts/start_dev.sh
```

开发页面由 Vite 输出到终端。比赛模式禁止使用该入口和 `--reload`。

## 比赛启动

地面站热点固定为 `10.42.0.1/24`，机载端固定为 `10.42.0.2/24`。依赖必须在赛前预热，
离线复核命令如下：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
uv sync --frozen --offline
cd frontend
corepack pnpm install --offline --frozen-lockfile
corepack pnpm build
cd /home/sb/Ground_station/.worktrees/nuedc-web
source runtime/web_ground_station.env
web_ground_station/scripts/check_web_ground_station.sh
web_ground_station/scripts/start_competition.sh
```

预检确认固定地址与端口、`uv.lock`、`pnpm-lock.yaml`、静态前端和 C++ 规划器均可用。
比赛启动不会安装依赖、不会启动热更新，也不会访问互联网。启动后在地面站 Chromium 打开
`http://10.42.0.1:8000`。

## 操作闭环

1. 确认顶部“命令 在线”和“遥测 在线”。遥测在线不能替代命令 ACK。
2. 进入“设置禁飞区”，选择三个横向或纵向连续格子，再生成航线。
3. 核对起点、巡查路径、下降起点和真实触地点，点击 LOAD。
4. 等待“已加载”，点击 START 并在确认框中确认。
5. 观察当前格、巡检数和检测总数；任务结束后核对“任务完成”和检测详情。
6. 需要中止时点击 STOP 并确认。浏览器刷新、断开或 Gateway 重启不会自动 STOP。

浏览器刷新后从 Gateway 快照恢复显示。Gateway 重启只从
`runtime/active_mission_plan.json` 恢复计划显示，必须等待新的机载 ACK 恢复“命令 在线”；
它不会自行恢复 running/loaded 状态，也不会替机载端决定飞行状态。

## 浏览器外紧急 STOP

浏览器不可用但 Gateway 主进程已退出时，可从另一个终端发送一次可靠 STOP：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
uv run python -m nuedc_web_gateway.emergency_stop \
  --task-id '<当前任务 ID>' \
  --env-file ../runtime/web_ground_station.env
```

仅在输出“紧急停止已确认”且退出码为 0 时视为机载端已 ACK。不要让该 CLI 与仍在运行的
Gateway 或 Qt 地面站长期并行发送命令。

## PlotJuggler PID 诊断

PID 波形调参不在 Web 地面站中重复实现。调试时让机载端继续把既有 UDP JSON 诊断流发送到
`10.42.0.1:9870`，在地面站启动 PlotJuggler，选择 UDP Streaming/JSON 解析并监听
`9870`。比赛运行时不需要启动 PlotJuggler。Gateway 和浏览器不监听、转发或保存 PID
波形，因此 PlotJuggler 是该端口唯一的地面端接收者。

## 验证

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
uv run pytest tests/gateway -v
uv run ruff check gateway tests/gateway
cd frontend
corepack pnpm test
corepack pnpm typecheck
corepack pnpm build
corepack pnpm playwright test
```

Playwright 动态选择测试用临时端口启动 `scripts/mock_airborne.py` 和生产 Gateway，覆盖完整任务闭环、
Gateway 重启、1024x600/1366x768/1920x1080 布局及无外部网络请求。
