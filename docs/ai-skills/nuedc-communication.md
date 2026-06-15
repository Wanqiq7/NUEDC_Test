# nuedc-communication

## 触发场景

当任务涉及 ZMQ、Protobuf、任务下发、心跳、重试、ROS2 bridge 或双 NUC 联调时使用。

## 工作规则

- 地面站下行命令通过 `ReliableCommandClient` 处理重试和状态。
- C++ 机载端与 ROS2 bridge 应复用同一套通用命令语义。
- Ack 文案必须能指导现场排错。

## 禁止事项

- 不在 UI 点击处理函数里手写重试循环。
- 不让 ROS2 bridge 依赖 H 题 UI 或 H 题模型。
- 不吞掉超时、连接失败或 protobuf 解析错误。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_reliable_command_client|test_zmq_command_client|test_task_protocol"
```

## 推荐提示词

请为这次通信变更补充失败优先的测试，覆盖首次超时后重试、连续失败离线、重复任务下发幂等和 Ack 错误文案。
