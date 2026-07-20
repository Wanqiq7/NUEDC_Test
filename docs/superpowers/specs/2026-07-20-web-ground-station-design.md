# Web 地面站全面重构设计

日期：2026-07-20  
状态：已完成需求审问，等待书面评审  
目标分支：`feat/web-ground-station`  
工作目录：`/home/sb/Ground_station/.worktrees/nuedc-web`

## 1. 背景与目标

现有地面站使用 Qt 6、C++17、ZeroMQ 和 Protobuf，已经完成 H 题的案例加载、
禁飞区编辑、路径规划、任务下发、执行控制、遥测、检测记录和任务汇总闭环。
它的主要问题不是功能缺失，而是四天三夜竞赛期间的开发反馈成本：Qt/C++ 修改经常
跨越头文件、实现文件和 UI 代码，完整构建耗时，并且实时图表和浏览器访问能力需要
额外工程量。

本设计在不改变机载任务权威和飞行安全语义的前提下，用 FastAPI Gateway 和 Vue 3
工作台替代 Qt 操作界面。路径规划继续由现有 C++ 实现负责；机载端继续使用现有
ZeroMQ + Protobuf 协议。Web 层使用 JSON、HTTP 和 WebSocket，降低日常调试和新增
诊断字段的成本。

成功标准：

1. Web 版本达到现有 Qt H 题功能等价。
2. 地面端日常开发不需要构建 Qt 应用；只有修改 C++ 规划器或共享协议时才构建 C++。
3. 比赛模式可以在无外网条件下，从地面热点 `10.42.0.1:8000` 使用。
4. 机载任务状态、命令幂等、断线和停止语义保持原工程行为。
5. `1024x600` 七英寸屏幕可以完成全部主控操作。

## 2. 范围

### 2.1 第一版包含

- 单操作员、单主控浏览器。
- 一个轻量赛题模块边界，第一版只实现 `h_problem`。
- H 题案例加载、禁飞区编辑、C++ 路径规划和 SVG 航线显示。
- MISSION_LOAD、START、STOP、PING 及对应 ACK 展示。
- 命令链路、遥测链路、当前位置、任务进度、检测结果和 Summary。
- Gateway 状态快照、WebSocket 增量推送和 JSONL 会话日志。
- 开发模式与比赛模式。
- 保留现有 UDP PID 诊断通道，波形调参使用 PlotJuggler，不在 Web 地面站重复实现调试页。
- 现有 mock airborne 联调与少量关键自动化测试。

### 2.2 第一版不包含

- 多操作员协同控制、用户认证、角色系统和审计数据库。
- Redis、消息队列、微服务、Nginx、Docker 和远程配置中心。
- SQLite/ORM、历史查询数据库和从磁盘恢复飞行运行态。
- 规划算法的 Python 或 TypeScript 重写。
- 机载 ZeroMQ/Protobuf 协议的整体 JSON 化。
- PWA、Electron、Capacitor、SSR 和离线浏览器安装。
- Three.js 三维姿态和视频作为 Web 接管 Qt 的前置条件。
- 复杂触摸手势和多浏览器控制权仲裁。

## 3. 总体架构

```text
Chromium / Chrome
  Vue 3 + TypeScript + Quasar
  HTTP commands + WebSocket state
             |
             | same-origin :8000
             v
FastAPI Gateway on ground NUC (10.42.0.1)
  Web API / snapshot / state mirror / JSONL
  C++ planner subprocess adapter
  ZeroMQ + Protobuf airborne adapter
             |
             | hotspot NUEDC-Ground
             v
Airborne NUC (10.42.0.2)
  ground_link :5557 PUB / :5558 REP
  mission_coordinator / ROS 2 / flight control
  optional PID UDP -> PlotJuggler at 10.42.0.1:9870
```

机载端是任务身份、任务是否加载、是否运行、飞行阶段和安全处置的权威。Gateway
只维护地面只读镜像、命令进行中状态和最近链路状态。浏览器不直接连接 ZeroMQ、
裸 TCP 或 ROS 2，也不自行推断任务已经开始或停止。

## 4. 仓库与 Git 边界

Web 实现在现有 `NUEDC_Test` 仓库内新增：

```text
NUEDC_Test/
├── web_ground_station/
│   ├── gateway/
│   ├── frontend/
│   ├── tests/
│   └── scripts/
├── shared/
├── ground_station_computer/   # 迁移期保留的 Qt 基线
└── runtime/
```

开发使用独立 worktree `/home/sb/Ground_station/.worktrees/nuedc-web` 和分支
`feat/web-ground-station`。当前 `main` 保持 Qt 稳定基线。第一阶段不修改
`Mid360_add_groundstation`；只有新增机载诊断字段或修改 `ground_link` 时，才在机载
仓库创建独立 worktree。

迁移期间不得同时运行 Qt 和 Web 两套控制发送端。Qt 保留为回退工具，Web 完成实机
闭环和稳定性验证后再单独决定归档，不在第一批提交中删除。

## 5. 技术栈

### 5.1 Gateway

- Python 3.10。
- FastAPI、Uvicorn、`asyncio`、`pyzmq`、Python Protobuf 和标准库 `json`。
- 少量 Pydantic 请求模型，只校验外部写接口。
- JSONL 持久化，不引入 SQLAlchemy、Alembic 或 SQLite。
- Pytest 和 Ruff。
- `uv` 管理环境，提交 `pyproject.toml` 和唯一的 `uv.lock`。

Gateway 不做企业级 API 网关。它只承担 Web 协议转换、当前状态镜像、原工程已有的
命令正确性约束、规划进程适配、广播和会话记录。

### 5.2 前端

- Vue 3、Vite、TypeScript、Quasar 基础组件。
- 一个 Pinia `groundStore` 和少量高频数据 composable。
- 原生 SVG 用于 H 题地图、禁飞区和航线。
- Vitest、Vue Test Utils 和 Playwright。
- `pnpm` 管理依赖，提交唯一的 `pnpm-lock.yaml`，`package.json` 固定
  `packageManager` 版本。

不使用 Vue Router。主控是一个单页多面板工作台。PID 调试不纳入 Web SPA，继续使用
现有 UDP 通道和 PlotJuggler。

TypeScript 保持 `strict: true`，但 `pnpm dev` 只运行 Vite，类型检查不阻塞热更新。
`pnpm typecheck` 在提交和阶段验收时运行。稳定命令、ACK 和快照使用明确类型；动态
诊断载荷使用 `Record<string, unknown>` 并在消费点校验。

## 6. C++ 无状态规划 CLI

路径规划继续以 `shared/cpp` 中现有 H 题实现为唯一权威。新增
`h_route_planner_cli`，它复用案例加载、禁飞区规则、规划器、坐标转换和 canonical
`TaskPlan` JSON codec，不复制规划逻辑。

CLI 使用版本化 JSON stdin/stdout：

```json
{
  "schema": "h_planning_request_v1",
  "case_path": "shared/cases/sample_case.json",
  "no_fly_cells": ["A2B3", "A2B4", "A2B5"]
}
```

成功输出：

```json
{
  "ok": true,
  "plan": {},
  "metrics": {}
}
```

失败输出：

```json
{
  "ok": false,
  "error_code": "invalid_no_fly_zone",
  "message": "..."
}
```

CLI 契约：

- `stdout` 只输出一个 JSON 文档，日志写 `stderr`。
- 退出码固定为：`0` 成功、`2` 请求/schema 无效、`3` 合法输入但无法规划、
  `4` 内部错误。
- 每次调用无共享状态，不直接写 runtime 文件。
- Gateway 使用 `asyncio.create_subprocess_exec`，设置超时、stdin 大小和 stdout 大小上限。
- Gateway 校验响应 schema、退出码和 canonical `TaskPlan`，成功后才原子写入
  `runtime/active_mission_plan.json`。
- 进程崩溃、超时、非法 JSON 或输出过大只导致本次规划失败，不影响 Gateway。

## 7. 机载协议策略

核心控制继续使用现有 ZeroMQ + Protobuf `Envelope`。保留 sequence、task_id、ACK、
mission_loaded、mission_running、last_accepted_sequence 和现有重试语义。Gateway 的
实现以原 Qt/C++ 测试锁定的行为为准，不重新定义安全策略。

动态业务和诊断字段使用协议现有的 JSON 逃生口：

- `TaskWaypointMessage.payload_json`
- `TaskPlanMessage.metadata_json`
- `TaskEventMessage.payload_json`
- `TaskSummaryMessage.payload_json`

只有新增核心命令或改变 Envelope/ACK 身份语义时才修改 `.proto`。例如新增
`gimbal_yaw_err`、PID 分量或视觉置信度时，只扩展 `payload_json`，不触发跨语言代码
生成。

Python Protobuf 文件必须在构建或环境同步阶段从仓库唯一的
`shared/proto/messages.proto` 生成到 Web 构建目录，不手写或提交第二份协议定义。

## 8. Gateway 状态和命令正确性

Gateway 不复制完整机载任务状态机，但必须保留以下最小机制：

- 一个 `asyncio.Lock` 串行化所有 ZeroMQ REQ/REP 命令。
- Qt 与 Web 发送端统一使用 `(Unix epoch ms << 20) + 进程内计数` 的单调 command
  sequence 契约；切换发送端前必须完全停止旧发送端，禁止并行发送。
- 有界超时、有限重试和 `last_accepted_sequence` 幂等确认。
- task_id 和当前任务隔离。
- 命令、遥测两条链路分别计算 TTL。
- 最近 ACK、TaskPlan、TaskEvent、TaskSummary 和错误快照。
- 服务端再次执行按钮前置条件，不能只依赖前端 debounce。
- Gateway 重启后先 PING 机载端，在有效 ACK 前显示“重新同步中”并禁止 START。

浏览器断开、刷新、Gateway 重启或短暂 Wi-Fi 丢包不自动停止飞行。飞控、定位、STM32
和机载控制链故障继续由机载安全状态机处理。保留一个不依赖浏览器和 FastAPI 的应急
STOP CLI，直接使用现有 ZeroMQ/Protobuf 命令链。

## 9. Web API

命令使用 HTTP，连续状态使用 WebSocket：

```text
GET  /api/health
GET  /api/snapshot
POST /api/mission/plan
POST /api/mission/load
POST /api/mission/start
POST /api/mission/stop
POST /api/link/probe
WS   /ws/telemetry
```

HTTP 响应是本次命令成功或失败的直接结果。WebSocket 随后广播新的状态。浏览器重连
流程固定为：建立连接、获取 `/api/snapshot`、记录快照序列号、再应用更新的增量消息。
snapshot 至少包含当前计划、命令/遥测链路、机载 ACK 镜像、运行态、当前位置、检测
汇总、最近 Summary 和最近错误。

Gateway 把 Protobuf 转成 Web 专用 JSON 信封，不向浏览器暴露 oneof、字段编号或生成
代码：

```json
{
  "schema": "nuedc.web.v1",
  "type": "task_event",
  "seq": 1842,
  "timestamp_ms": 1760000000000,
  "task_id": "mission-001",
  "event": "telemetry",
  "payload": {
    "current_cell": "A3B2",
    "visited_cells": 5
  }
}
```

高频 telemetry、PID、姿态和电机状态使用 latest-value/有限环形缓冲，允许丢弃中间
帧。Gateway 必须把关键离散事件、Summary、ACK 和错误应用到状态镜像并写入 JSONL，
且按机载 `Envelope.sequence` 去重。Web `seq` 由 Gateway 在独立、安全整数范围内单调
生成，不能混入 command 或机载序列域。浏览器断线期间可以错过增量广播，但重连后的 snapshot 必须包含
这些事件产生的当前状态；WebSocket 不承担无限事件回放。JSONL 记录 Gateway 实际收到
的完整消息，不受浏览器刷新频率影响。

## 10. 持久化与恢复

不引入数据库。持久化文件为：

```text
runtime/active_mission_plan.json
runtime/sessions/<timestamp>.jsonl
```

计划使用临时文件 + 原子替换。JSONL 通过有界异步队列写入，慢磁盘不得阻塞命令和
遥测处理；文件按会话和大小滚动。

Gateway 重启后：

1. 可以加载最后一个计划用于显示。
2. 不从磁盘恢复 `mission_running`、`mission_loaded` 或链路在线状态。
3. 主动 PING 机载端，以 ACK 重建 task_id 和运行镜像。
4. 收到有效 ACK 前禁止 START。
5. 当前会话检测统计可以从 JSONL 重算，但磁盘记录不决定飞行状态。

## 11. PID 诊断兼容策略

PID 波形调参继续复用机载现有 UDP JSON 诊断通道，目标端口为 `10.42.0.1:9870`，
由 PlotJuggler 负责接收、显示和调参。Gateway 和 Web 前端不监听该端口、不维护 PID
窗口，也不增加 PID 页面；这样可以直接复用已有联调工具，减少比赛现场的启动和维护步骤。

## 12. 前端状态与交互

单个 Pinia `groundStore` 包含：

```text
connection / mission / runtime / telemetry / detections / diagnostics / ui
```

`/api/snapshot` 是初始状态来源，WebSocket 应用增量。命令按钮不得乐观修改
mission_running 等权威字段；只有 HTTP ACK 或服务端推送可以更新。Pinia 不使用持久化
插件。高频遥测序列放入专用 composable，避免整棵 store 高频重渲染。

H 题地图使用 SVG：

- 9x7 网格格子支持点击和基本触摸切换。
- 航线使用 polyline，起点、下降起点和真实触地点使用不同标记。
- `viewBox` 保持比例，提供明确的缩放和适配视图按钮。
- 前端只显示 C++ CLI 输出，不实现路径规划规则。

关键操作目标至少 `44x44 px`，不依赖 hover，不把拖拽作为唯一交互。关键命令确认和
按钮门控沿用原工程语义。第一版不实现双指缩放等复杂手势。

## 13. 响应式布局

主控目标是 Chromium/Chrome，完整支持：

- `1920x1080`：标准双栏。
- `1366x768`：紧凑双栏。
- `1024x600`：七英寸完整操作布局。

`1024x600` 下顶部状态条和底部 LOAD/START/STOP 命令栏保持可见；地图有稳定尺寸和
宽高比；次要日志、检测详情进入抽屉或对话框。字体使用固定档位，不按视口连续缩小。
PID 波形调参由 PlotJuggler 提供；Web 地面站不承载该调试工作区。

## 14. 配置、网络与启动

比赛网络继续使用原工程拓扑：地面 NUC 创建 `NUEDC-Ground` 热点，地址
`10.42.0.1`；机载 NUC 作为客户端，地址 `10.42.0.2`。机载 PUB/REP 继续监听
5557/5558。标准比赛入口为：

```text
http://10.42.0.1:8000
```

Gateway 绑定 `0.0.0.0:8000`，前端使用同源 API、WebSocket 和静态资源。所有字体、
图标和脚本本地打包，不使用 CDN。

配置继续使用环境变量和 `runtime/web_ground_station.env`：

```text
NUEDC_AIRBORNE_HOST
NUEDC_TELEMETRY_PORT
NUEDC_COMMAND_PORT
NUEDC_PID_DEBUG_ENABLED
NUEDC_PID_DEBUG_PORT
NUEDC_WEB_HOST
NUEDC_WEB_PORT
NUEDC_RUNTIME_DIR
NUEDC_PLANNER_CLI
```

`NUEDC_PID_DEBUG_ENABLED` 和 `NUEDC_PID_DEBUG_PORT` 仅作为既有配置契约的兼容字段保留；
当前 Web Gateway 不创建 UDP PID 接收器，也不绑定 `9870`。通用 `pid_debug` 任务事件仍可经
现有 ZeroMQ/Protobuf 链路进入状态镜像，但波形 UDP 流直接由 PlotJuggler 接收。

提供开发、比赛两个启动入口。开发模式运行 Vite 和 Uvicorn reload；比赛模式先构建
前端，由 FastAPI 同源托管 `dist`，禁止 reload，且不启动 PID UDP 接收器。比赛启动沿用现有网络
预检语义，且不执行下载、升级或在线检查。

依赖工作流统一为 `pnpm + uv`，不保留 npm/pip/Poetry 项目流程或第二份 lockfile。
赛前预热 pnpm store 与 uv cache，并完成完全断网的安装、预检和启动演练。

## 15. 错误处理

- 规划输入错误：HTTP 4xx，返回稳定 error_code 和中文操作提示。
- 规划 CLI 超时/崩溃/非法 JSON：HTTP 5xx，本次规划失败，不替换现有计划。
- 命令超时：保留原重试和 already-accepted 判断；无法确认时显示未知，不伪造成功。
- 遥测断线：命令链路独立计算，不以 telemetry 推断命令在线。
- WebSocket 断线：前端显示 stale，指数退避重连，恢复后重新获取 snapshot。
- 畸形 `pid_debug` 事件：按通用任务事件解析规则丢弃或记录，不中断 Gateway。
- JSONL 写入失败：状态栏提示记录降级，但不得阻塞飞行命令。
- task_id 不匹配或旧序列消息：安静丢弃并写调试日志，不污染当前任务。

## 16. 测试与验收

### 16.1 最小自动化测试

- C++ CLI：合法规划、非法禁飞区、缺失案例、stdin schema、stdout 纯 JSON、退出码。
- Gateway：CLI 超时/崩溃/非法 JSON、ZMQ ACK、幂等重试、TTL 和重启同步。
- PID 兼容：通用 `pid_debug` 事件 TTL 和配置默认值；不测试或实现 Web UDP 端口绑定。
- 前端 store：snapshot、增量遥测、旧 task_id 丢弃、命令失败不误改运行态。
- SVG：格子选择、航线、下降起点和触地点。
- Playwright：一条 LOAD -> START -> telemetry -> Summary 闭环。
- Playwright 视口：1920x1080、1366x768、1024x600，检查重叠和关键按钮可见。
- 迁移期间继续运行现有 C++/Qt 测试。

不设置覆盖率指标，不为 Quasar 本身写单测，不做全浏览器矩阵或大量脆弱截图快照。

### 16.2 Web 接管 Qt 的门槛

1. 固定案例规划输出符合 C++ 基线和 canonical `TaskPlan`。
2. mock airborne 完成 LOAD、START、telemetry、detection 和 Summary。
3. 浏览器刷新、WebSocket 重连和 Gateway 重启可以恢复权威状态。
4. 应急 STOP CLI 可独立使用。
5. 1024x600 可完成规划、下发、执行、停止和结果查看。
6. 连续运行测试无命令串扰、状态漂移或无界内存增长。
7. 完成无外网、地面热点拓扑的实机预检和联调。

## 17. 兼容与迁移顺序

实现遵循增量替代：

1. 新增 C++ 规划 CLI，不改变现有 Qt 调用路径。
2. 新增 Gateway，并用现有 mock airborne 验证协议。
3. 新增 Web 主控界面，实现 Qt 功能等价。
4. 增加开发/比赛启动和网络预检。
5. 完成三种视口、断线恢复和实机闭环验证。
7. Web 通过接管门槛后，将 Qt 标记为回退实现；是否删除另行决策。

该顺序避免在同一阶段同时重写地面 UI、路径规划算法和机载协议。任何阶段失败时，
现有 Qt 和机载协议仍可提供已知可用的回退路径。
