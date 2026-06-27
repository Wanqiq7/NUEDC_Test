# 分布式 Qt/ZeroMQ 风险修复进度

## 2026-06-27
- 建立执行计划，启动三个只读审查子代理：命令链路、ROS bridge、UI/存储。
- 修复命令链路 sequence 原子性：handler 级互斥包住 stale 检查、副作用和成功接受。
- 修复 ReliableCommandClient stale Ack 幂等成功判定，并补充 mission load/start/stop/wrong-target 测试。
- 修复 ROS bridge Ack 字段、状态同步、ZMQ REP socket 线程亲和性，并更新转换层测试。
- 优化 detection UI 路径：内存增量汇总、实时列表上限、数据库析构释放顺序。
- 清理 `proto_messages` AUTOMOC 和 `build-*/` 忽略规则。
- 构建并通过测试：test_h_command_handler、test_reliable_command_client、test_detection_repository、test_main_window、test_zmq_command_client、test_task_protocol、test_airborne_runtime、test_mission_command_service。
- 最终子代理审查因外部额度 403 失败，已由主代理完成本地静态审查和验证收口。