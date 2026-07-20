# nuedc-communication

## 触发场景

当任务涉及 ZMQ、Protobuf、ROS2 bridge、任务同步、心跳、重试或掉线恢复时使用。

## 允许修改

- `web_ground_station/gateway/nuedc_web_gateway/**`
- `web_ground_station/tests/gateway/**`
- `shared/cpp/src/protocol/**`
- `shared/cpp/tests/test_task_protocol.cpp`

## 工作规则

- 优先把同步能力做成幂等、可重试、可观测。
- 任务下发和状态确认必须保留清晰错误信息。
- Gateway 负责 Protobuf/ZMQ 到 Web JSON/WebSocket 的专用转换。

## 禁止事项

- 不在通信层解析题目 UI 状态。
- 不把一次性成功当作稳定。
- 不忽略离线、超时、重复下发和重连后的状态恢复。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_task_protocol"
cd web_ground_station && uv run pytest tests/gateway -q
```

## 推荐提示词

请增强通信链路可靠性，说明重试、心跳、幂等和离线恢复如何落地，并列出受影响的测试。
