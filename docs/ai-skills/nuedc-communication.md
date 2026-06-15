# nuedc-communication

## 触发场景

当任务涉及 ZMQ、Protobuf、ROS2 bridge、任务同步、心跳、重试或掉线恢复时使用。

## 允许修改

- `ground_station_computer/src/framework/communication/**`
- `ground_station_computer/src/h_problem/mission/h_mission_command_service.*`
- `airborne_computer/src/**`
- `airborne_computer/ros2/nuedc_bridge/**`
- `shared/cpp/src/protocol/**`
- `shared/cpp/tests/test_h_command_handler.cpp`

## 工作规则

- 优先把同步能力做成幂等、可重试、可观测。
- 任务下发和状态确认必须保留清晰错误信息。
- C++ 命令处理和 ROS2 bridge 尽量复用同一套核心语义。

## 禁止事项

- 不在通信层解析题目 UI 状态。
- 不把一次性成功当作稳定。
- 不忽略离线、超时、重复下发和重连后的状态恢复。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_reliable_command_client|test_mission_command_service|test_zmq_command_client|test_h_command_handler"
```

## 推荐提示词

请增强通信链路可靠性，说明重试、心跳、幂等和离线恢复如何落地，并列出受影响的测试。