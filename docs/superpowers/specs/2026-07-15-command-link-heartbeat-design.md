# 地面站命令链路真实心跳设计

## 目标

将地面站的机载状态从“最后一次命令 ACK 后 5 秒即离线”改为基于真实、持续的
REQ/REP 心跳判定。地面站应在机载端稳定运行时持续显示在线；网络或命令服务真正失联时，
应在连续 3 次心跳失败后安全地显示离线并禁用控制。

本设计只覆盖地面站命令链路健康度。ZeroMQ 端口、Protobuf 协议、机载 ROS 2 节点、任务
状态机和遥测 TTL 均不修改。

## 已确认问题

当前 `MainWindow` 在启动和用户点击“刷新机载链路”时调用 PING。成功 ACK 会记录一个时间戳，
但每秒运行的定时器只刷新 UI，不会发送下一次 PING。`kCommandLinkTtlMs` 为 5000 ms，故任何
静置超过 5 秒的健康链路都会显示离线。

该行为不是热点断开：联调环境中到机载端的 30 次 ping 零丢包，热点未发生 NetworkManager
重连，且机载端 5557/5558 可达。

## 方案比较

### 方案 A：在 UI 定时器中同步 PING

每 2 秒从 `MainWindow` 的 `QTimer` 回调直接调用现有同步 `ReliableCommandClient::ping`。
实现量小，但一次机载端无响应会让 UI 线程最长阻塞 3 次 x 1.5 秒及重试间隔；地图、状态和
停止按钮会暂时无响应，不采用。

### 方案 B：专用后台心跳监视器（采用）

新增 `CommandLinkMonitor`，由专用 `QThread` 在后台执行 PING。它每 2 秒开始一次检查，使用
现有 `ReliableCommandClient` 的幂等重试策略，并通过 Qt signal 将结果回传 UI 线程。所有实际
ZeroMQ 发送经由共享的串行传输层，因而后台 PING 不会与现有 UI 命令并发占用机载 REP。

一次失败只表示本轮未确认，不立即判为离线；连续 3 次失败才进入离线。任一次有效 ACK 立即
清零失败计数并恢复在线。这样既防止静置误报，又不把遥测 PUB 当作命令 REQ/REP 的健康证明。

### 方案 C：由遥测持续到达表示在线

遥测为机载 PUB、地面 SUB，命令为机载 REP、地面 REQ；两条链路独立。遥测到达不能证明任务
下发、停止或视觉控制可用，因此不采用。

## 状态语义

命令链路状态统一为以下三类：

- `Online`：最近一次 PING 或用户命令收到有效 ACK。
- `Checking`：已有最近成功 ACK，但最新 1 或 2 次后台 PING 未确认；界面显示“机载状态：链路确认中”。
- `Offline`：连续 3 次后台 PING 或显式命令发送均未获得有效 ACK；界面显示“机载状态：离线”。

控制安全规则：

- `Online` 才允许依赖命令确认的执行、停止和视觉控制。
- `Checking` 和 `Offline` 均禁用这些控制；任务本地显示与遥测继续更新。
- 用户的任务下发、开始、停止、视觉武装/复位仍沿用已有的 3 次重试和 ACK 幂等确认。用户命令
  成功时立即将监视器恢复为 `Online`；失败时作为一次失败样本处理，而非直接覆盖成离线。
- “刷新机载链路”请求一次立即心跳；它不会并发启动第二个心跳请求。

心跳参数固定为：间隔 2000 ms、失败阈值 3、现有单次 ZMQ 收发超时 1500 ms、现有可靠重试
最多 3 次和间隔 120 ms。实现中需确保前一轮尚未完成时不启动重叠 PING。

## 架构与并发

### CommandLinkMonitor

`CommandLinkMonitor` 负责且仅负责命令链路健康检查：

- 在所属工作线程内持有自己的 `ReliableCommandClient`；该客户端使用共享的
  `SerializedCommandTransport`，避免跨线程使用同一 `ReliableCommandClient` 实例。
- 接收 `start(task_id)`、`stop()` 和 `probeNow(task_id)` 请求。
- 用 `QWaitCondition` 在专用线程中以 2000 ms 等待后触发 PING；立即刷新请求唤醒该等待。每轮
  PING 完成后才开始下一轮，避免 REP 队列与 sequence 交错。
- 发布 `healthChanged(CommandLinkHealth health, int consecutive_failures, QString detail)` 信号。
- 接收 UI 发出的 `recordCommandResult(bool success, QString detail)`，使用户命令成功可立即复位失败
  计数，失败只计为一个样本。

`MainWindow` 仍是所有 UI、任务同步和控制可用性的唯一所有者。它不再拥有命令健康倒计时，也不在
UI 线程执行自动 PING。窗口析构时先停止监视器线程，再销毁命令传输对象，防止关闭时访问已释放
的 Qt 对象。

为避免后台心跳和用户命令同时占用机载 REP，`SerializedCommandTransport` 以互斥锁包裹每次
底层 `sendEnvelope`。`CommandLinkMonitor` 和既有 `MissionCommandService` 分别拥有自己的
`ReliableCommandClient`，但共享该传输层；因此重试语义保持不变，物理 ZMQ 请求不会并发。
心跳只在上一轮完成后开始；用户命令若等待中的心跳，最多等待当前一次既有 1.5 秒收发超时，不改变
用户命令的 ACK、幂等或任务状态处理方式。

## UI 文案与恢复

- `Online`：`机载状态：在线`，保留现有遥测子状态，例如 `遥测：已接收`。
- `Checking`：`机载状态：链路确认中（1/3）` 或 `（2/3）`，并附现有遥测子状态。
- `Offline`：`机载状态：离线`，并保留最后一次失败详情于底部状态栏。
- PING 成功后不要求用户点击刷新或重启；状态立即恢复为在线。
- 初始启动前没有成功 ACK 时显示 `机载状态：检查中`，首次心跳连续失败 3 次后显示离线。

## 错误处理与兼容性

- 沿用现有 Protobuf `Envelope.ack` 合约、命令 sequence 和 `ReliableCommandClient` 的 stale ACK
  确认逻辑。
- 网络不可达、端口拒绝、ZMQ 超时、无效 ACK 均视为单次失败，详情原样传给状态栏。
- 机载端忙于 ROS 服务调用时，短暂超时不会立即显示离线；连续失败后才降级。
- 不以 TCP 端口探测或 telemetry 收到情况提升命令链路健康度。
- 本变更不放宽真实飞控安全条件：无有效命令 ACK 时仍不允许发送执行和视觉控制。

## 验收与测试

新增和调整地面站 Qt Test 覆盖：

1. 初始 PING 成功后，跨过原 5 秒 TTL 仍保持 `Online`，且监视器确实发出后续 PING。
2. 连续一次、两次失败均为 `Checking`，按钮禁用但状态不是 `Offline`。
3. 第三次连续失败转为 `Offline`，状态栏包含最后失败详情。
4. `Checking` 或 `Offline` 后任一 PING 成功立刻恢复 `Online` 并清零失败计数。
5. 用户任务命令成功会重置失败计数；一次用户命令失败不绕过三次阈值。
6. 刷新按钮发起立即 PING，但在同一时刻最多一个命令请求在飞行中。
7. 机载端无响应期间，Qt UI 事件循环仍可处理控件刷新，证明自动心跳没有阻塞 UI 线程。
8. 现有任务下发、START/STOP、视觉武装和 stale ACK 幂等测试继续通过。

联调验收：机载仿真保持运行时，地面站静置至少 30 秒持续显示在线；停止机载 `ground_link` 后，
地面站在最多 3 个心跳周期内显示离线；恢复机载端后，无需重启地面站即可显示在线。

## 非目标

- 不修改 Wi-Fi 热点、静态 IP、DHCP 或防火墙设置。
- 不更改 5557/5558 端口、ZeroMQ 模式或 Protobuf 字段。
- 不将遥测链路状态与命令链路状态合并。
- 不改变机载端 START 的真实飞控执行器安全策略。
