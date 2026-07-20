# nuedc-architect

## 触发场景

当任务涉及架构边界、目录归属、通用框架、题目隔离或长期维护性时使用。

## 允许修改

- `shared/cpp/include/competition_core/**`
- `shared/cpp/src/**`
- `web_ground_station/gateway/**`
- `web_ground_station/frontend/src/**`
- `web_ground_station/tests/**`

## 工作规则

- 先判断变更属于 `competition_core`、题目 core、Gateway、前端 UI 还是协议通信。
- Gateway 只承载协议转换、连接状态和持久化边界，飞行决策留在机载端。
- 高频状态允许覆盖中间帧，任务事件和 ACK 必须可靠保留。

## 禁止事项

- 不允许把题目专有模型放进 `competition_core`。
- 不允许前端直接解析 Protobuf 或持有 ZMQ 连接。
- 不允许新增题目时复制 Gateway 通信主流程。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_task_ports|test_task_protocol"
cd web_ground_station && uv run pytest tests/gateway -q
```

## 推荐提示词

请审查这次变更属于共享 C++ 核心、Gateway、前端 UI 还是机载协议层，指出跨层耦合风险并给出最小改造方案。
