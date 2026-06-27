# 分布式 Qt/ZeroMQ 风险修复发现记录

## 已修复
- `CommandState` 增加命令处理互斥锁入口，`handleEnvelopeCommand()` 将 stale 检查、业务副作用、成功后 sequence 接受放进同一临界区，避免旧命令并发覆盖新命令，也避免失败 mission load 提前占用 sequence。
- `ReliableCommandClient` 在 stale Ack 中根据 `last_accepted_sequence` 和目标状态识别幂等成功，覆盖 mission load/start/stop。
- ROS bridge Ack 字段补齐 `task_id/mission_loaded/mission_running/last_accepted_sequence`，REP socket 改为命令线程内创建使用，命令状态和 sequence 用 mutex 保护。
- ROS bridge detection event 改为调用方传入事件序号，不再固定为 0。
- `HProblemTaskAdapter` 改为内存增量汇总 detection totals，实时列表上限 500 条，避免每条 detection 全表聚合和无限增长。
- `DetectionRepository` 析构前释放 `QSqlDatabase` 成员句柄再 `removeDatabase()`，并让 `open()` 对已打开连接幂等。
- `proto_messages` 明确关闭 AUTOMOC，`.gitignore` 增加 `build-*/`。

## 剩余风险
- detection 写 SQLite 仍在 UI 路径同步执行；本次已去掉最重的全表聚合和无限列表，但真正高频场景下仍建议后续引入后台批量写入 worker。
- ROS2 bridge 未在本机 ROS2/colcon 环境下编译运行验证。