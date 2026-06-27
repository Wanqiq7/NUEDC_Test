# 分布式 Qt/ZeroMQ 风险修复执行计划

## 目标
落实架构审查中发现的高风险点：命令序列号幂等/原子性、ROS bridge 协议漂移和线程亲和性、UI detection 高频路径、数据库连接生命周期、构建配置噪声。

## 阶段
1. [完成] 核对现状与测试入口。
2. [完成] 修复命令 sequence 原子接受与 ReliableCommandClient stale Ack 幂等成功判定。
3. [完成] 同步 ROS bridge Ack/状态语义并修复 ZMQ socket 线程亲和性。
4. [完成] 优化 detection UI 路径并修复 DetectionRepository 析构。
5. [完成] 清理 CMake/.gitignore 并运行验证。
6. [完成] 汇总子代理审查结果并完成最终 review。

## 验收结果
- 主 CMake 受影响目标已构建通过。
- 8 个相关 Qt/C++ 测试通过：test_h_command_handler、test_reliable_command_client、test_detection_repository、test_main_window、test_zmq_command_client、test_task_protocol、test_airborne_runtime、test_mission_command_service。
- ROS2 bridge 属于独立 ROS2/ament 工程，当前 Windows 主 CMake 未覆盖其编译；本次完成源码一致性修复和转换层测试更新，但未在 ROS2 环境中运行 colcon。