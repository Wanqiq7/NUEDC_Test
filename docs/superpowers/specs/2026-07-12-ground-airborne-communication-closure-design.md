# 地面站与机载端通信闭环设计

## 目标

修复任务通信中五类已确认问题：虚假执行 ACK、非运行期遥测污染运行态、缺少完成汇总、跨任务消息串扰和遥测在线状态永久不失效。

本轮不猜测 STM32 固件协议。仓库内没有航点目标下行接口，生产端在没有真实飞行执行器时必须拒绝 `START_MISSION`，不能继续返回成功。

## 方案比较

### 方案 A：执行器适配器并默认失败关闭（采用）

在机载通信桥和飞控之间定义小型 `FlightExecutor` 接口。任务加载只负责验证和保存；开始命令只有在执行器可用且接受当前 TaskPlan 时才成功。执行器通过到点反馈推进航点，并在真实完成后触发 `TaskSummary`。

优点是不会伪造飞行成功，单元测试可以使用假执行器完整验证状态机，后续接 STM32 或其他飞控时不改变 ZMQ/Protobuf 协议。缺点是当前固件协议接入前，实机 START 会明确失败。

### 方案 B：直接发布通用 ROS Pose 目标

桥接节点直接发布 `PoseStamped`。当前工作区没有任何节点订阅此目标，发布成功不等于飞行执行，因此仍是假闭环，不采用。

### 方案 C：扩展现有 STM32 串口帧

现有串口代码只上送定位和视觉数据，仓库没有对应固件和目标点帧定义。自行增加字段可能造成飞控误动作，不采用。

## 地面站状态规则

- telemetry 只更新当前位置和遥测内容，不改变任务运行态。
- 运行态仅由 START/STOP ACK 和当前任务 Summary 改变。
- TaskEvent、TaskSummary 和带任务信息的 ACK 必须属于当前任务；空任务 ID 或其他任务消息不进入 H 题控制器。
- 遥测在线状态使用最后接收时间和 5 秒 TTL，不再使用永久布尔值。
- 不匹配消息安静丢弃并写调试日志，避免旧任务数据污染数据库和 UI。

## 机载状态规则

- MissionLoad 成功后替换当前任务，并复位 running、stop、vision、进度和汇总统计。
- START/STOP/ARM/RESET 必须携带当前任务 ID；PING 不要求任务 ID。
- START 必须同时满足任务已加载、执行器可用、执行器接受任务。否则返回失败 ACK，且不接受该 sequence。
- telemetry 只在任务运行期间发布；里程计仍持续缓存供定位和执行器使用。
- STOP 调用执行器取消并清除 running/vision，但不伪造成功完成 Summary。
- 只有执行器报告任务完成时才发布成功 TaskSummary，并清除 running/vision。
- Summary 记录唯一访问格数和本任务识别汇总。

## 组件边界

### FlightExecutor

负责真实飞控交互，接口至少包含可用性检查、开始 TaskPlan、停止任务和里程计更新。执行器通过回调报告 waypoint 到达、成功完成或失败。

通信桥不解释具体串口或飞控协议，只把经过验证的任务交给执行器，并把执行状态转换为 protobuf 事件和 ACK。

### Ground Runtime State

`HMissionController` 负责当前任务数据隔离；`MainWindow` 负责物理遥测链路 TTL。任务运行态与链路在线态保持独立。

## 错误处理

- 无执行器：`START_MISSION` 返回 `flight executor is not configured`。
- 任务 ID 不匹配：返回 `command task_id does not match active mission`。
- 未加载任务：返回 `mission is not loaded`。
- 执行器拒绝或失败：返回错误 ACK 或失败事件，不设置 running。
- protobuf、TaskPlan 验证和持久化失败继续沿用现有明确错误回复。

## 测试

- 地面站：telemetry 不启动任务；STOP 后 telemetry 不恢复运行；其他任务事件、Summary、ACK 不改变当前任务；遥测 TTL 到期。
- 通用命令处理器：未加载 START、错误 task_id、任务替换状态复位、STOP/视觉命令任务隔离。
- 机载桥状态机：无执行器 START 失败；假执行器 START 成功；非运行期不发布任务 telemetry；STOP 取消；完成时只发布一次 Summary。
- 继续运行地面站通信测试、ROS 三包构建测试和本机 ZMQ 回环测试。

## 兼容性

protobuf 字段和端口保持不变。行为变化是此前错误返回成功的 START 在没有飞控执行器时改为失败；这是有意的安全收紧。
