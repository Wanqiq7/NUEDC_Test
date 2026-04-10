# H 题野生动物巡查系统混合联调测试平台设计

## 目标

在 `/home/qwe/XT` 中建立一套可直接运行的比赛验证工程，用于模拟 2025 年电赛 H 题的关键流程：

- 根据禁飞区生成覆盖航线
- 机载端按航线推进并发送遥测
- 在动物所在方格触发识别结果上报
- 地面站实时显示、统计并写入 SQLite
- 在一次联调中验证“规划、通信、显示、存储、汇总”主链路

该工程用于验证架构与流程，不用于直接替代最终比赛嵌入式地面站。

## 架构

系统采用双进程真实联调：

- `aircraft_sim/`：Python 机载模拟端
- `ground_station/`：Qt6 Widgets 桌面地面站
- `proto/`：Protobuf 消息协议
- `cases/`：可重复执行的比赛案例

进程间通信统一采用 `ZeroMQ + Protobuf`：

- Python 端作为 `PUB`
- Qt 端作为 `SUB`
- 第一版只实现机载到地面站的单向上报链路

## 功能边界

### 第一版必须完成

1. 支持加载一个测试案例，案例包含：
   - 禁飞区三连格
   - 起飞格
   - 动物列表与所在方格
2. 根据禁飞区生成覆盖所有可巡查格的蛇形航线
3. Python 模拟端按固定时间步发送：
   - 任务配置
   - 飞行遥测
   - 动物识别结果
   - 汇总结果
4. Qt 地面站显示：
   - `9x7` 栅格
   - 禁飞区
   - 巡查路径
   - 当前巡查位置
   - 实时动物结果列表
   - 动物统计汇总
5. Qt 地面站将识别结果保存到 SQLite，并支持程序内查询显示

### 第一版不做

- 反向控制链路
- 真实视觉识别
- 真实飞控接入
- 激光光斑几何验证
- 降落逻辑

## 数据模型

### 测试案例

案例文件采用 JSON，仅作为离线输入：

- `case_id`
- `start_cell`
- `no_fly_cells`
- `animals`
- `tick_interval_ms`

### Protobuf 消息

统一使用 `Envelope` 包裹：

- `message_type`
- `sequence`
- `timestamp_ms`
- `payload`

最小消息集合：

- `GridConfig`
- `Telemetry`
- `AnimalDetection`
- `MissionSummary`

## 模块设计

### Python 机载模拟端

- `case_loader.py`：加载与校验案例
- `route_planner.py`：生成覆盖航线
- `publisher.py`：ZeroMQ 发送
- `simulator.py`：按 tick 推进状态并触发消息

### Qt 地面站

- `MainWindow`：主窗口与整体布局
- `GridScene`：地图绘制与状态更新
- `ZmqSubscriberWorker`：后台接收线程
- `MessageDispatcher`：Protobuf 解包与分发
- `DetectionRepository`：SQLite 持久化

## 测试策略

### Python 测试

使用 `unittest` 验证：

- 禁飞区外全覆盖
- 三连格禁飞区不会被规划进航线
- 命中动物格时会产出识别消息
- 汇总结果与案例一致

### C++ 测试

使用 `Qt Test` 验证：

- 地图坐标与格子编码转换
- SQLite 入库与汇总统计
- Protobuf 包解码

### 联调验证

通过启动模拟端和地面站，验证：

- 地面站收到初始配置
- 实时更新当前巡查格
- 检测结果实时显示
- 程序结束后汇总与数据库一致

## 风险与取舍

- 由于题目最终地面站不能使用 PC，本工程只作为开发验证平台，不代表最终参赛形态
- `PUB/SUB` 足以验证上报链路，但不代表最终比赛可只用单通道
- 第一版优先稳定复现流程，不追求过度复杂的动画和交互
