# 双 NUC 通信设计方案

## 背景

当前仓库已经完成了以下基础能力：

- 地面站端可手动设置三格禁飞区，并本地生成新航线
- 地面站端会将当前任务计划持久化到 `runtime/active_mission_plan.json`
- 机载模拟端启动时可优先读取 `runtime/active_mission_plan.json`
- 机载端通过 `ZeroMQ + Protobuf` 向地面站持续上报遥测与识别结果

也就是说，现阶段系统已经具备：

- **本地生成任务**
- **本地持久化任务**
- **单向上报遥测**

但还缺少真正的双机闭环：**地面站 NUC 把任务计划下发到机载 NUC，并收到明确确认**。

本轮的目标不是改 UI，也不是重写现有通信栈，而是在当前 `ZeroMQ + Protobuf` 基础上补齐“地面站下发 / 机载确认”的最小双机通信方案。

## 目标

实现以下最小闭环：

1. 地面站端生成任务计划
2. 地面站端将任务计划通过网络发送给机载 NUC
3. 机载 NUC 接收并校验任务计划
4. 机载 NUC 将任务计划写入本地 `runtime/active_mission_plan.json`
5. 机载 NUC 返回确认消息
6. 地面站端收到确认后，认为任务已同步成功
7. 机载端继续沿用现有上行 `PUB/SUB` 链路发送 `GridConfig`、`Telemetry`、`AnimalDetection`、`MissionSummary`

## 已确认方案

采用 **`ZeroMQ over TCP + Protobuf` 双通道方案**。

### 方案对比

#### 方案 A：继续使用 `ZeroMQ + Protobuf`

上行：

- 机载 NUC -> 地面站 NUC
- `PUB/SUB`

下行：

- 地面站 NUC -> 机载 NUC
- `REQ/REP`

优点：

- 复用当前已有的 `publisher.py`、`zmq_subscriber_worker.cpp`、`messages.proto`
- 双机联调改动最小
- 对比赛工程最稳
- 不需要引入额外 Web 或 RPC 框架

缺点：

- 需要在现有协议里补充确认消息
- 需要机载端增加命令接收服务

#### 方案 B：改成原始 TCP Socket + Protobuf

优点：

- 最底层，可完全控制

缺点：

- 要自己处理分帧、收发边界、超时、错误恢复
- 改造成本显著高于当前方案
- 对当前项目没有明显收益

#### 方案 C：改成 HTTP / WebSocket

优点：

- 若未来要接浏览器端或云端，可扩展性较强

缺点：

- 当前系统两端都不是浏览器
- 不适合现有持续遥测流和比赛环境
- 属于更换通信范式，风险大

### 推荐结论

选择 **方案 A**。  
理由：当前仓库已经有稳定的上行 `ZeroMQ + Protobuf` 链路，最合理的扩展方式是补一条下行控制链路，而不是更换整套通信框架。

## 网络拓扑

建议两台 NUC 处于同一局域网，并使用固定 IP。

示例：

- 地面站 NUC：`192.168.10.10`
- 机载 NUC：`192.168.10.20`

### 端口规划

- `5557`：机载 -> 地面站，上行 `PUB/SUB`
- `5558`：地面站 -> 机载，下行 `REQ/REP`

### 绑定方式

#### 机载端

- `PUB bind tcp://0.0.0.0:5557`
- `REP bind tcp://0.0.0.0:5558`

#### 地面站端

- `SUB connect tcp://<airborne_ip>:5557`
- `REQ connect tcp://<airborne_ip>:5558`

这种方式更适合比赛环境：

- 机载端作为任务执行者，固定对外提供服务
- 地面站只需要知道机载 NUC 的 IP 并连接即可

## 协议设计

### 现有可复用消息

当前 [messages.proto](/home/qwe/XT/shared/proto/messages.proto) 中已经定义了：

- `GridConfig`
- `Telemetry`
- `AnimalDetection`
- `MissionSummary`
- `Envelope`

其中 `GridConfig` 已足够承载任务计划的核心内容：

- `case_id`
- `start_cell`
- `no_fly_cells`
- `route`
- `terminal_cell`
- `landing_enabled`
- 降落相关参数

因此，下行任务计划**不需要新建另一套 mission schema**，直接复用 `GridConfig` 最合适。

### 新增消息

本轮建议最小扩展一个确认消息：

#### `Ack`

- `bool success`
- `string message`

### `Envelope` 扩展建议

在 `oneof payload` 中新增：

- `GridConfig mission_load = 14`
- `Ack ack = 15`

如果你更偏向最小字段改动，也可以直接把下行任务继续复用 `grid_config = 10`，但从长期可维护性看，**建议将“上行回显用的 GridConfig”与“下行下发用的 mission_load”做语义区分**，即使底层结构相同。

## 组件职责设计

## 地面站端

### 新增组件：`ZmqCommandClient`

建议新增文件：

- `ground_control/src/zmq_command_client.h`
- `ground_control/src/zmq_command_client.cpp`

职责：

- 建立到机载端 `tcp://<airborne_ip>:5558` 的 `REQ` 连接
- 将地面站本地生成的任务计划编码为 Protobuf
- 发送任务计划到机载端
- 同步等待 `Ack`
- 返回成功 / 失败与错误信息

### `MainWindow` 的职责变化

地面站主窗口应在“航线生成成功”后，不再只做本地预览，而是分为两步：

1. 本地生成任务计划并更新地面站 UI
2. 调用 `ZmqCommandClient` 下发任务到机载端

只有在机载端返回 `Ack(success=true)` 后，状态栏才显示：

- `状态: 任务已同步至机载端，可执行任务`

若发送失败，则显示：

- `错误: 任务已本地生成，但机载端未确认`

### 与现有本地文件的关系

地面站端仍应保留：

- `runtime/active_mission_plan.json`

作用：

- 本地预览缓存
- 用户手工检查当前任务计划
- 在网络发送前作为地面站端本地状态落盘

也就是说：  
**网络下发不是替代本地文件，而是建立在本地文件之上的同步行为。**

## 机载端

### 新增组件：`command_server.py`

建议新增文件：

- `airborne/uav_testbed/command_server.py`

职责：

- 监听 `tcp://0.0.0.0:5558`
- 接收地面站下发的任务计划
- 解析 Protobuf
- 校验关键字段
- 写入 `runtime/active_mission_plan.json`
- 返回 `Ack`

### 校验规则

机载端收到任务计划后，至少校验：

- `route` 非空
- `start_cell` 存在
- `terminal_cell` 存在
- `no_fly_cells` 中的格子代码合法
- `route` 中不包含禁飞格

第一版不要求机载端重新规划，只要求它确认“这是一份结构正确、可执行的计划”。

### 与模拟端入口的关系

当前 [run_simulator.py](/home/qwe/XT/airborne/uav_testbed/run_simulator.py) 已支持：

- 优先读取 `runtime/active_mission_plan.json`

因此本轮机载端只要把收到的任务计划成功落盘，模拟端/执行端就已经能消费这份计划。

这意味着：

- 机载端命令服务新增后
- 现有模拟器入口可以继续复用

这是本轮方案的一个重要优点。

## 时序设计

### 任务下发时序

1. 用户在地面站设置禁飞区
2. 地面站生成新的 `GridConfig`
3. 地面站写本地 `runtime/active_mission_plan.json`
4. 地面站通过 `REQ` 发送任务计划
5. 机载端通过 `REP` 收到任务计划
6. 机载端校验并写自己的 `runtime/active_mission_plan.json`
7. 机载端返回 `Ack`
8. 地面站收到 `Ack(success=true)`，显示“已同步机载”

### 后续执行时序

在最小版本里，可以允许机载端以“读取本地 runtime 文件后执行”的方式工作，而不立即实现 `START_MISSION`。  
也就是：

- 第一步先解决“任务同步”
- 第二步再解决“执行控制”

## 错误处理

### 地面站侧错误

- 连接不到机载端：
  - 提示 `机载端未在线或端口不可达`
- 发送超时：
  - 提示 `任务发送超时`
- 收到 `Ack(success=false)`：
  - 将 `Ack.message` 直接显示给用户

### 机载端错误

- Protobuf 无法解析：
  - 返回 `Ack(false, "invalid protobuf")`
- 任务字段缺失：
  - 返回 `Ack(false, "missing route/start/terminal")`
- 文件写入失败：
  - 返回 `Ack(false, "cannot persist mission plan")`

### 容错策略

第一版不做：

- 自动重试
- 多次重连
- ACK 重传
- 序列号幂等控制

这些都可以在后续增强阶段再做。

## 测试策略

### 机载端测试

新增 Python 测试覆盖：

- 合法任务计划可成功写入 runtime 文件
- 缺少 `route` 返回失败
- 非法格子代码返回失败
- `route` 命中禁飞格返回失败

### 地面站端测试

新增 Qt 测试覆盖：

- `ZmqCommandClient` 能序列化并发送任务计划
- 收到 `Ack(success=true)` 时返回成功
- 超时时返回失败
- `Ack(success=false)` 时错误文案透传

### 集成测试

最终至少验证一条双机/伪双机链路：

1. 地面站发送任务计划
2. 机载端保存 `runtime/active_mission_plan.json`
3. 模拟器按新计划启动

## 分阶段落地顺序

### 阶段 1：最小任务同步闭环

实现：

- 协议增加 `Ack`
- 机载端 `command_server.py`
- 地面站端 `ZmqCommandClient`
- `MainWindow` 在生成后执行下发

验收标准：

- 地面站能把任务送到机载端
- 机载端能落盘
- 地面站收到确认

### 阶段 2：执行控制

新增：

- `START_MISSION`
- `STOP_MISSION`
- 可选 `PING`

### 阶段 3：稳定性增强

新增：

- 重试
- 心跳
- 状态查询
- 幂等处理

## 不在本轮范围内

本轮不做：

- 多机编队通信
- 云端中继
- HTTP / WebSocket 重构
- 自动重连状态机
- 任务执行细粒度暂停 / 恢复
- 加密传输

## 结论

本轮双 NUC 通信的最优方案是：

- **继续沿用 `ZeroMQ + Protobuf`**
- **保留现有上行 `PUB/SUB`**
- **新增下行 `REQ/REP`**
- **复用 `GridConfig` 作为任务计划**
- **新增 `Ack` 作为确认消息**

这条路径能在最小改动下，把当前“本地任务生成 + 本地落盘 + 单向遥测”升级为真正的“双机任务同步 + 单向遥测回传”闭环。
