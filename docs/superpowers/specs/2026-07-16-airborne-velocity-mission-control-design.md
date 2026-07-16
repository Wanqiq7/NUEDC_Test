# 机载速度闭环任务控制设计

## 背景

当前系统把地面站生成的完整 `TaskPlan` 通过 `/nuedc/execute_mission` Action 交给
`flight_control_adapter`，后者再尝试将任务编码给 STM32。这与实机职责不符：STM32
只执行机体系期望速度，任务推进、航点判断和安全决策都应位于机载 NUC。

本设计替代 `2026-07-12-ground-airborne-communication-closure-design.md` 中的
`FlightExecutor` Action 执行方案，但保留其中已经建立的命令序列、任务身份、视觉会话和
汇总隔离规则。

## 目标

- 地面站只负责禁飞区与完整任务目标序列规划。
- `mission_coordinator` 直接使用 LIO 位姿运行 XYZ 位置 PID、推进航点并发布机体系速度。
- STM32 只执行现有 23 字节串口帧中的 `vx/vy/vz`，不理解任务、航点或 yaw。
- 正常完成、STOP、定位故障和命令超时均具有确定且可测试的输出。
- 仿真与实机使用同一 `/nuedc/velocity_command` 控制接口。
- 地面站、视觉格网和 LIO 使用同一米制场地坐标。
- 旧厘米计划在进入闭环前被明确拒绝，不能依赖人工保证部署版本一致。

## 非目标

- 不修改 STM32 串口帧结构、校验方式或波特率。
- 不向 STM32 下发 yaw；yaw 由 STM32 自行保持为 0。
- 不实现降落 LED 闪烁。
- 不增加障碍物在线重规划、轨迹平滑或加速度前馈。
- 除旧计划兼容门禁外，不对 H 题计划增加与飞行控制无关的严格几何校验；地面站负责生成正确动作顺序。
- 不在下降阶段增加斜线偏差、触地点 XY 或下降角异常终止条件。

## 方案比较

### 方案 A：协调器直接闭环并发布速度（采用）

`mission_coordinator` 持有任务状态、定位、PID 和当前航点，固定 10 Hz 发布
`geometry_msgs/msg/Twist`。串口适配器只做命令缓存、超时保护和帧编码。

该方案使决策状态与任务权威集中在同一组件，STM32 和仿真端共享同一个速度接口；代价是
需要删除现有 Action 生命周期代码，并迁移 `/nuedc/flight_state` 的任务进度发布权。

### 方案 B：保留独立 MissionFollower Action

新增一个机载航点跟踪 Action 服务端，协调器仍通过 Action 管理开始和取消。该方案边界清晰，
但在本项目中会形成第二套任务状态机，增加 START、STOP、失败锁存和 Summary 的竞态，因此不采用。

### 方案 C：由 STM32 执行航点

将整条航线、到点判定和任务进度交给 STM32。该方案违反“STM32 只执行速度”的硬件职责，
且需要新增固件任务协议，因此不采用。

## 坐标系

### 场地坐标 F

- 原点：`A9B1` 格心。
- `+X`：从 `B1` 指向 `B7`。
- `+Y`：从 `A9` 指向 `A1`。
- `+Z`：向上。
- 单位：米。

格心坐标为：

```text
x = (B序号 - 1) * 0.5
y = (9 - A序号) * 0.5
```

基准点：

```text
A9B1 = (0.0, 0.0)
A1B1 = (0.0, 4.0)
A9B7 = (3.0, 0.0)
A1B7 = (3.0, 4.0)
```

### 机体系 B

- `+X`：机头方向。
- `+Y`：机体左侧。
- `+Z`：向上。
- yaw 绕 `+Z`，俯视逆时针为正。

无人机在 `A9B1` 初始化，机头朝向 `B7`；LIO 初始化位姿约定为
`x=0, y=0, z=0, yaw=0`，但软件不校验首帧是否接近该数值。

PID 先生成场地系速度，再使用 LIO 当前 yaw 转换为机体系速度：

```text
vx_body =  cos(yaw) * vx_map + sin(yaw) * vy_map
vy_body = -sin(yaw) * vx_map + cos(yaw) * vy_map
vz_body =  vz_map
```

STM32 不接收 yaw，也不参与该变换。

### 地面站内部厘米坐标

地面站现有 `cellCenterCm()` 同时服务规划几何、禁飞区和 UI，不改变其语义。仅在构造
执行 `TaskPlan` 时转换：

```text
x_m = (350.0 - internal_y_cm) / 100.0
y_m = (450.0 - internal_x_cm) / 100.0
```

任意触地点使用相同转换：

```text
x_land = (350.0 - touchdown_y_cm) / 100.0
y_land = (450.0 - touchdown_x_cm) / 100.0
```

`metadata_json` 中带 `_cm` 后缀的规划与 UI 字段继续使用厘米；`TaskWaypoint.x/y/z`
统一表示米制执行坐标。

## TaskPlan 契约

Protobuf 已包含 `x/y/z/action`，无需修改 `messages.proto`。H 题计划使用一个有序
`waypoints` 数组表达完整执行过程：

```text
sequence 0: A9B1, action="takeoff", x=0, y=0, z=1.20
sequence 1..N: 格网航点, action="navigate", z=1.20
sequence N+1: touchdown, action="land", x/y=规划触地点, z=0
```

- 首个路线格 `A9B1` 直接改为 `takeoff`，不重复添加同一格。
- `start_waypoint_id` 保持为 `A9B1`。
- `terminal_waypoint_id` 指向唯一 ID `touchdown`，表示整个任务的最终落点。
- `metadata_json.execution_contract` 固定为 `"h_field_m_v1"`，明确坐标单位、坐标系和动作语义。
- `metadata_json.terminal_cell` 继续表示最后一个巡查格。
- 最后巡查格由序列中最后一个 `action="navigate"` 的航点识别。
- `visited_waypoints` 按 `sequence_index` 去重，统计 takeoff、navigate 和 land。
- UI 格网航线只提取 takeoff 与 navigate；touchdown 单独作为降落终点显示。

Protobuf schema 不增加字段。`ground_link` 使用
`google::protobuf::util::JsonStringToMessage()` 将现有 `TaskPlanMessage.metadata_json` 解析为
`google::protobuf::Struct`，只提取 `execution_contract`，写入 ROS `TaskPlan.msg` 新增的同名
字符串字段；ROS 无关的 domain `TaskPlan` 同样保存该字段，不新增第三方 JSON 依赖。H 题
地面站验证器与机载任务加载器都必须要求 `execution_contract == "h_field_m_v1"`。H 题 metadata
不是合法 JSON、字段不是字符串、字段缺失或值不匹配时，LOAD 直接拒绝，不得进入 PID；其他
task type 不解释该值。

降落动作和触地点仍只由结构化 waypoint 的 `action/x/y/z` 表达，运行控制绝不从 metadata
推断 land。厘米旧计划和米制新计划在二进制层都合法，因此仍需原子升级并重新生成 `runtime`
与 mock 任务文件；旧的全 navigate、`z=0` 计划即使残留也会被兼容门禁拒绝。

## ROS 数据流

```text
ground_link -- /nuedc/load_mission, /nuedc/control_mission --> mission_coordinator
LIO ---------------- /aft_mapped_to_init -------------------> mission_coordinator
cam_vision ---------- /nuedc/animal_tracking_active --------> mission_coordinator

mission_coordinator -- /nuedc/velocity_command (Twist) -----> flight_control_adapter
mission_coordinator -- /nuedc/flight_state -----------------> ground_link / vision
flight_control_adapter -- /nuedc/flight_control_ready ------> mission_coordinator
flight_control_adapter -- 现有23字节串口帧 ---------------> STM32
```

仿真模式将 `flight_control_adapter` 替换为 `flight_control_sim`：

```text
mission_coordinator -- Twist --> flight_control_sim -- Odometry --> mission_coordinator
flight_control_sim -- ready=true --> mission_coordinator
```

### `/nuedc/velocity_command`

- 类型：`geometry_msgs/msg/Twist`。
- 仅使用 `linear.x/y/z`，语义为机体系期望速度，单位 m/s。
- QoS：Reliable、KeepLast(1)、Volatile，禁止新订阅者收到旧任务速度。
- 唯一发布者：`mission_coordinator`。
- 正常发布周期：100 ms。

### `/nuedc/flight_control_ready`

- 类型：`std_msgs/msg/Bool`。
- QoS：Reliable、KeepLast(1)、Volatile，避免新协调器读取退出节点留下的 `true`。
- 实机由串口适配器每 100 ms 发布一次心跳；值表示串口已打开且最近一次写入未失败。
- 仿真端以相同周期发布 `true`。
- 协调器以 `steady_clock` 记录本机接收时间。START 要求最近 500 ms 内收到 `true`；收到
  `false` 或 Running 中超过 500 ms 未收到新心跳时进入 Failed。
- Failed 后协调器继续锁存失败下降命令；若适配器或串口已经失联，实际兜底由 STM32 本地
  指令超时完成。

该状态只能证明 NUC 串口端可写，不能证明 STM32 已收到或执行。

### `/nuedc/flight_state`

改由 `mission_coordinator` 唯一发布，内容包括：

- 最新有效 LIO pose 与实际 velocity。
- 当前执行目标 ID 和 sequence。
- 已完成目标数。
- execution ID、任务模式和错误码。
- `connected` 取 `/nuedc/flight_control_ready`。

其中 `connected = last_ready_value && heartbeat_age <= 500 ms`；不能只复用缓存的最后一个
布尔值，否则心跳静默退出后可能出现 `phase=Failed` 但 `connected=true`。

`FlightState.header.stamp` 必须原样复制当前 pose 对应的 LIO Odometry 源时间戳，不能填写
协调器发布时间；`header.frame_id` 同样沿用定位消息。这样视觉端计算 detection 与 pose 的
时间差时不会把陈旧位姿误判为新数据。尚无首帧有效 LIO 时不发布可供视觉使用的 pose 状态。

`ground_link` 与 `vision_mission_adapter` 继续消费该话题，无需改变外部事件格式。

### Action 接口退役

- 从 `nuedc_interfaces` 的生成列表中删除 `ExecuteMission.action`。
- 在 `nuedc_interfaces/msg/TaskPlan.msg` 和 domain `TaskPlan` 增加 `execution_contract`；
  `ground_link` 从 Protobuf 的 metadata JSON 结构化提取并透传该值。
- 删除 `mission_coordinator` 的 Action client、attempt watchdog 和相关生命周期测试。
- 删除 `flight_control_adapter` 与 `flight_control_sim` 的 Action server。
- 删除三个包不再使用的 `rclcpp_action` 依赖。
- `MissionPhase::Starting` 与 `Stopping` 的既有数值保留为兼容占位，但新控制路径不进入这两个阶段。
- 新增 `PHASE_FAILED=6` 与 `PHASE_CANCELLED=7`，同步到 `MissionContext.msg` 和所有消费者测试。

## 组件设计

### 地面站任务生成

`buildTaskPlan()` 保留厘米制路线和降落几何，在执行计划边界完成以下操作：

1. 将路线首格构造为 takeoff，目标高度取 `landing.cruise_height_cm / 100`。
2. 将后续巡查格构造为 navigate，使用米制场地坐标和相同高度。
3. 将规划触地点转换为米制坐标，追加 ID 为 `touchdown` 的 land 航点。
4. 将 `terminal_waypoint_id` 改为 `touchdown`。
5. 保留 metadata 中最后巡查格、厘米触地点、下降距离和角度，供 UI 与报告使用。

H 题协议适配器只对 takeoff+navigate 子序列进行格网覆盖、禁飞区和相邻性校验；land
不作为格子参与覆盖校验。它仍检查所有坐标为有限值，并检查 land 与 metadata 指向同一个
规划触地点，但不把 `45°±5°` 规划几何变成机载降落失败条件。地面站与机载端都先校验
`execution_contract`，再解释航点数值。

UI 中最后巡查格的语义改为“下降起点”，touchdown 才是“降落终点”。触地点坐标为零时仍是
合法值，控制器和 UI 不得再用 `coordinate != 0` 判断字段是否存在。

### `MissionFollower`

新增 ROS 无关的 `MissionFollower`，职责是：

- 保存规范化计划、当前 sequence 和已完成 sequence 集合。
- 持有 X、Y、Z 三个 `PidController`。
- 按当前 action 选择完成条件。
- 输出场地系目标速度、机体系目标速度和进度变化。
- 在目标切换时不重置 PID 状态。

每次 START 都创建新的 execution 状态：sequence 回到 0、visited 清空，三个 PID 的积分和
测量历史重置。这里的“目标切换不重置”只指同一次 execution 内从一个 waypoint 推进到下一个，
不跨任务或跨 execution 保留控制器历史。

完成条件使用严格小于：

| action | 控制 | 完成条件 |
| --- | --- | --- |
| takeoff | XYZ PID，XY 保持起飞点 | `abs(z-target_z) < 0.05 m` |
| navigate | XYZ PID，高度保持 1.20 m | `hypot(ex,ey) < 0.10 m` |
| land | XYZ PID 指向 touchdown | `z < 0.05 m` |

达到条件时，当前 sequence 计入 visited，并在下一控制周期使用下一目标。land 完成后立即
产生成功结果，即使仍存在 XY 残差。

若运行时遇到未知 action，任务进入 Failed，而不是猜测其语义。

### PID

三个轴使用 C++17 `PidController`。实现为独立重写，不复制无许可证的外部 C 源码。

默认参数：

```text
Kp = 1.0
Ki = 0.0
Kd = 0.0
dt = 0.1 s
derivative_on_measurement = true
deadband = 0
```

微分先行公式为：

```text
D = -Kd * (measurement - previous_measurement) / dt
```

因此目标切换不产生 D 冲击。当前 `Kd=0`，该能力留待实机调参。积分、输出限幅、抗饱和
和滤波由同一 PID 接口提供；切换目标不清除积分或微分历史。

正常 takeoff/navigate：

- X、Y 各自独立限幅 `abs(output) <= 0.30 m/s`，不限制 XY 合速度。
- Z 独立限幅 `abs(output) <= 0.30 m/s`。
- `/nuedc/animal_tracking_active=true` 且视觉已 ARM 时，X、Y PID 限幅动态改为
  `0.12 m/s`；Z 保持 `0.30 m/s`。
- START、STOP、Failed、Cancelled 和 Completed 均复位跟踪限速状态为 false。

上述 `0.30 m/s` 是场地系 X/Y PID 的单轴输出限制。map-to-body 旋转后不再逐分量裁剪，
否则会改变期望速度方向；在约定的 `yaw=0` 飞行中，场地系与机体系分量相同。若 yaw 偏离，
正常段单个机体系分量理论上可达到 `sqrt(2)*0.30 ~= 0.424 m/s`，串口编码与 STM32 必须允许
该数值，但这不改变 PID 的场地系单轴限幅定义。

land 阶段是唯一的 XY 合速度限幅例外：

- X/Y PID 先生成水平向量，再按方向等比例缩放，使水平合速度不超过 `0.30 m/s`。
- Z PID 仍独立限制在 `0.30 m/s`。
- 当水平与垂直输出同时饱和时，名义下降角为 45°；XY 对角方向对应
  `vx_map=vy_map=0.30/sqrt(2) ~= 0.212 m/s`、`vz_map=-0.30 m/s`。
- 地面站仍允许 `45°±5°` 的规划几何；控制器不要求水平距离严格等于 1.20 m，也不保证
  全过程精确保持 45°。水平合速度整形只定义命令上限，不参与到点或失败判定；轨迹偏差由
  实机调参承担。

### 定位保护

定位回调逐帧执行校验，控制定时器不跳过中间消息：

- 实际使用的 x/y/z 和四元数必须有限，四元数必须可归一化。
- Running 中收到非有限定位时立即 Failed，包括尚未建立首个有效基线的阶段。
- 还没有收到任何定位消息时，START 后持续发布零速度等待，不启动 1 s 超时。
- 第一帧有效定位只建立基线，不校验是否接近 `(0,0,0,0)`。
- 后续时间戳必须严格递增；倒退立即 Failed，重复时间戳不更新基线，最终由超时处理。
- 每一对相邻且时间戳严格递增的定位帧都直接参与运动学检查，不做累计、平滑或降采样。
- 相邻位置推算的 XY 合速度大于 `2.5 m/s` 时立即 Failed。
- 相邻速度向量推算的 XY 合加速度大于 `5.0 m/s²` 时立即 Failed。
- 首帧有效定位后，使用 `steady_clock` 计算接收新鲜度；超过 1 s 未更新时 Failed。

速度与加速度使用 Odometry header stamp 计算，超时只使用本机单调时钟，禁止混用时钟域。

### 任务状态机

保留现有 phase 数值并新增 `Failed` 与 `Cancelled`：

```text
Empty -> Loaded -> Running -> Completed
                    |  |
                    |  +-> Cancelled
                    +----> Failed
```

- START：要求任务已加载且 flight control ready；内部执行器立即接受任务，phase 进入 Running。
- Running 但尚无定位：持续 `(0,0,0)`。
- 正常 land 完成：进入 Completed，发布成功 Summary，此后持续 `(0,0,0)`，人工上锁。
- STOP：立即进入 Cancelled，发布失败 Summary，此后持续 `(0,0,-0.3)`。
- 定位或飞控链路故障：立即进入 Failed，发布失败 Summary，此后持续 `(0,0,-0.3)`。
- Failed/Cancelled 为当前机载任务系统运行周期内的锁存状态，拒绝 LOAD、START、ARM 和
  RESET；PING 保留。
- `mission_coordinator` 禁止自动 respawn；只重启该节点不属于受支持的恢复操作。必须终止并
  重新启动完整 `airborne_bringup` 任务系统后，才能创建新的任务执行状态并清除锁存。实机操作
  默认通过重新开机完成这一过程。
- Completed 保留现有重新加载或重新执行能力。

进入 Completed、Failed 或 Cancelled 的同一次串行状态转换会先关闭 vision，再冻结统计并发布
Summary。已经在 ROS 通道中但尚未由协调器处理的迟到 observation 将被拒绝，不增加额外排空
等待；这是确定的完成边界，不影响 `z < 0.05 m` 后立即结束控制任务。

STOP 只在 Running 接受；等待首帧定位也属于 Running，因此此时 STOP 仍会下发
`(0,0,-0.3)`。这是已确认的操作语义，上板时必须避免在未解锁起飞前误触 STOP。

`ground_link` 对 Failed/Cancelled ACK 映射为
`mission_loaded=false, mission_running=false`，避免地面站继续显示可执行；task ID 和 Summary
仍用于说明终止任务。

### `flight_control_adapter`

删除 Action 服务端、`MissionExecutor`、`FlightProtocol`、串口任务状态解码和独立
`Stm32SpeedLimiter`。保留：

- 串口打开与单写入所有权。
- LIO 位置缓存，用于现有帧的位置字段。
- `/nuedc/velocity_command` 的最新有效命令与本机接收时间。
- 现有 23 字节编码、XOR 校验和 500000 baud 配置。

串口帧前三个字段只来自 Twist 期望速度，绝不来自 `odometry.twist`。位置字段使用最新有效
LIO 位置；尚无定位时使用零，定位丢失后使用最后有效位置。命令发送不依赖定位是否存在。

200 ms 看门狗使用基于 `steady_clock` 的一次性 deadline wall timer：每次收到有效 Twist 都把
deadline 重置为本机接收时刻加 200 ms；timer 回调重新核对 deadline，到期后只写一次零速度帧
并将缓存保持为零。它不依赖轮询周期，也不等待下一个 100 ms 正常发送周期。新的有效 Twist
重新解除超时并建立新 deadline；正常串口命令仍以 10 Hz 发送。Linux/ROS 调度不是硬实时，
STM32 的 300 ms 本地超时仍是最终硬件兜底。

任何非有限 Twist 被拒绝并立即按零速度处理。串口写失败发布 ready=false。

### `flight_control_sim`

删除 Action 服务端和按时间跳航点的 `SimulatedMission`。新仿真器：

- 启动即发布初始 `(0,0,0,yaw=0)` Odometry 和 ready=true。
- 订阅与实机相同的 `/nuedc/velocity_command`。
- yaw 固定为 0，因此机体系与场地系重合。
- 使用单调时钟实际 `dt` 积分 XYZ，并发布 `/aft_mapped_to_init`。
- 复现 200 ms 命令超时；超时后速度归零，避免协调器退出后继续运动。
- 不自行理解航点、任务或到点条件。

### 格网与视觉

`nuedc_domain::GridGeometry` 改用 A9B1 格心为原点的场地系语义。B 序号只决定 X，A 序号
只决定 Y；有效范围是 `B=1..7`、`A=1..9`。正向变换与 TaskPlan 完全一致：

```text
x = (B - 1) * 0.5
y = (9 - A) * 0.5
```

反向变换按格心外扩半格，以左/下闭、右/上开区间归属格子：

```text
B = floor((x + 0.25) / 0.5) + 1
A = 9 - floor((y + 0.25) / 0.5)
有效边界：-0.25 <= x < 3.25，-0.25 <= y < 4.25
```

实现不再使用含糊的“9 列、7 行”去推断轴语义，也不再把 A9B1 映射到 `(0.25,0.25)`。

`vision_mission_adapter` 继续使用 `FlightState.pose` 映射动物方格；边界以格心外扩半格处理，
使四角格中心及有效方格内部均可反查。对应配置和全部 round-trip 测试同步更新。

## 配置

`mission_coordinator.yaml` 新增或替换为：

```yaml
mission_coordinator:
  ros__parameters:
    control_period_ms: 100
    odometry_topic: /aft_mapped_to_init
    velocity_command_topic: /nuedc/velocity_command
    flight_control_ready_timeout_ms: 500
    localization_timeout_ms: 1000
    max_localization_xy_speed_mps: 2.5
    max_localization_xy_acceleration_mps2: 5.0
    waypoint_xy_tolerance_m: 0.10
    takeoff_z_tolerance_m: 0.05
    landing_complete_height_m: 0.05
    emergency_descent_speed_mps: -0.30
    xy_pid.kp: 1.0
    xy_pid.ki: 0.0
    xy_pid.kd: 0.0
    xy_pid.normal_output_limit_mps: 0.30
    xy_pid.tracking_output_limit_mps: 0.12
    z_pid.kp: 1.0
    z_pid.ki: 0.0
    z_pid.kd: 0.0
    z_pid.output_limit_mps: 0.30
    landing.horizontal_speed_limit_mps: 0.30
```

`flight_control.yaml` 保留串口参数并新增：

```yaml
    velocity_command_topic: /nuedc/velocity_command
    command_send_period_ms: 100
    command_timeout_ms: 200
    ready_publish_period_ms: 100
```

删除 adapter 中的 `normal_linear_speed` 和 `tracking_linear_speed`，限幅参数只属于 PID。

## 并发模型

- `mission_coordinator` 使用单线程执行器或同一 MutuallyExclusive callback group 串行处理
  服务、定位、跟踪状态和控制 timer。
- 定位样本不得并发或乱序处理，否则会误触发时间倒退保护。
- 串口写只有一个执行上下文；正常发送和看门狗零帧通过同一互斥入口。
- Summary、Context 和当前航点在同一次状态转换后发布，避免地面站看到跨代组合。
- Twist 看门狗使用本机 `steady_clock`，不使用 ROS 仿真时间。

## 硬件前提

以下两项不改变串口格式，但都是上板前硬门禁；任一项未通过就不得解锁飞行：

1. STM32 将速度字段按有符号 16 位补码解释。`vz=-0.30 m/s` 编码为 `-300` 的低 16 位，
   若固件按无符号数解析会产生危险指令。
2. STM32 自身具有速度命令接收超时保护：连续 300 ms 未收到校验正确的速度帧时，将
   `vx/vy/vz` 期望值原子置零并继续保持 yaw。NUC 或串口已经失效时无法再送达下降帧，零速
   是不引入任务决策、且仍符合“STM32 只执行速度”的本地兜底。

由于现有串口没有 ACK，NUC 只能知道设备是否打开和 `write()` 是否失败，不能证明 STM32
已经执行期望速度。

## 测试设计

### 地面站

- 四角格心和任意触地点的厘米内部坐标到米制执行坐标转换。
- 生成 takeoff、navigate、land，动作、XYZ、sequence 和两个终点语义正确。
- Protobuf 与 JSON round-trip 保留动作、米制坐标和 touchdown terminal ID。
- 缺少或写错 `h_field_m_v1` 的旧厘米 H 题计划在地面站和机载 LOAD 两端均被拒绝。
- H 题路线覆盖与相邻性排除 land，UI route 不包含 touchdown。
- land 与 metadata 触地点不一致、非有限坐标和未知 action 的失败路径。
- simulator 不把 touchdown 当格子，visited 包含 takeoff 和 land。
- runtime/mock 文件使用新契约。

### `nuedc_domain`

- PID 比例、积分限幅、输出限幅、滤波和显式 dt。
- 微分先行忽略目标跳变，但响应测量变化。
- takeoff、navigate、land 的完成条件和严格边界等号。
- visited 按 sequence 去重并包含全部 action。
- yaw 为 `0、±90°、π` 的 map-to-body 转换。
- 正常/跟踪 X/Y 单轴限幅、Z 限幅和 landing 水平合速度限幅。
- 首帧等待、1 s 超时、NaN/Inf、无效四元数、重复与倒退时间戳。
- 速度 2.5 与加速度 5.0 的等号边界和超限，以及不规则周期和极短正 `dt` 的逐帧比较。
- Failed/Cancelled 锁存且持续输出下降命令。

### ROS 节点

- START 后 10 Hz 零速等待首个有效定位。
- 各 action 推进、FlightState、Context 和 Summary 恰好一次。
- tracking 动态修改 PID limit，所有终止路径复位。
- STOP、定位故障、ready 丢失和正常完成的 phase 与命令。
- ready 的 Volatile QoS、100 ms 心跳、500 ms 新鲜度边界与节点静默退出。
- FlightState 保留 LIO 源时间戳，视觉新鲜度判断不使用协调器发布时间。
- 新 START 清空 sequence/visited/PID 历史，同一 execution 内切 waypoint 不重置 PID。
- 终态前已进入 ROS 队列、终态后才处理的迟到 observation 不改变冻结的 Summary。
- Twist 三轴准确进入旧帧，Odometry twist 永不进入命令字段。
- 无 Odometry 仍能发送零速或下降命令。
- 200 ms 看门狗边界、恢复和非有限 Twist。
- deadline timer 每帧重置、到期只写一次零帧，且不等待 10 Hz 正常发送 timer。
- 仿真速度积分、初始里程计、yaw=0 和命令超时。

### 系统验收

- `takeoff -> navigate -> land` 仿真闭环完成，visited 等于完整目标数。
- 最终 `z<0.05 m` 后持续零速度。
- STOP、定位超时、定位跳变分别进入锁存下降。
- 视觉跟踪期间 X/Y 单轴速度不超过 0.12 m/s。
- ROS graph 中不存在 `/nuedc/execute_mission`。
- bringup 中 coordinator `respawn=false`；Failed/Cancelled 后只有完整任务系统重启才恢复接令。
- 地面站任务下发、机载 ACK、实时 telemetry、检测和 Summary 保持闭环。

## 迁移顺序

1. 固化共享 PID、定位保护、格网坐标和 MissionFollower 的纯 C++ 测试。
2. 修改地面站 TaskPlan 生成与 UI 适配，重新生成运行时计划。
3. 修改 ROS TaskPlan 消费、状态机和 coordinator 速度闭环。
4. 将 adapter 改为 Twist 到旧串口帧，并加入 200 ms 看门狗。
5. 将 simulator 改为速度积分模型。
6. 更新 bringup、系统测试和部署文档。
7. 完成仿真闭环后，以自动测试或示波/串口回放确认 STM32 有符号速度与 300 ms 本地超时，
   两项门禁通过后再上板解锁联调。

两个仓库必须在同一部署窗口升级；禁止新机载端执行旧地面站生成的厘米计划。

## 预计文件边界

### `NUEDC_Test`

- `shared/cpp/src/mission/mission_planning.cpp`：生成米制 takeoff/navigate/land。
- `shared/cpp/include/h_problem_core/planning/mission_geometry.h` 与对应 `.cpp`：提供内部厘米坐标到执行坐标转换。
- `ground_station_computer/src/h_problem/mission/h_protocol_adapter.cpp`：区分格网路线与 touchdown。
- `ground_station_computer/src/h_problem/mission/h_mission_controller.cpp`：向 UI 传递下降起点和真实触地点。
- `ground_station_computer/src/h_problem/ui/`：修正下降起点与降落终点显示语义。
- `shared/cpp/src/runtime/simulator.cpp`：按 sequence 处理非格网 land 并统计全部目标。
- `runtime/`、README 与双 NUC 部署文档：更新新计划契约和启动验证。

### `Mid360_add_groundstation`

- `src/nuedc_domain/`：PID、LocalizationGuard、MissionFollower、坐标和锁存任务状态。
- `src/nuedc_interfaces/`：退役 ExecuteMission Action，扩展 MissionContext phase 常量，并在
  ROS `TaskPlan` 增加 `execution_contract`。
- `src/mission_coordinator/`：订阅定位与跟踪状态，运行 10 Hz 闭环并发布 Twist、FlightState 和 Summary。
- `src/flight_control_adapter/`：移除任务 Action，订阅 Twist，执行串口编码和 200 ms 看门狗。
- `src/flight_control_sim/`：用速度积分模型替代航点跳转模型。
- `src/ground_link/`：结构化提取并透传 execution contract，更新 Failed/Cancelled 的 ACK 映射。
- `src/nuedc_airborne/airborne_bringup/`：同步实机/仿真节点参数、依赖与系统测试。
