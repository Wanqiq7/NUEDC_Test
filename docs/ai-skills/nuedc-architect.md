# nuedc-architect

## 触发场景

当任务涉及架构边界、目录归属、通用框架、题目隔离或长期维护性时使用。

## 允许修改

- `shared/cpp/include/competition_core/**`
- `shared/cpp/src/**`
- `ground_station_computer/src/framework/**`
- `ground_station_computer/tests/**`
- `docs/framework_architecture.md`
- `docs/adding_task_adapter.md`

## 工作规则

- 先判断变更属于 `competition_core`、题目 core、地面站 framework、题目 UI、机载 runtime 还是 ROS2 bridge。
- 优先保持 Shell 薄，题目逻辑只能通过 Adapter / Runtime / Codec 进入。
- 任何新增题目都必须注册到适配器工厂，默认选择由 `NUEDC_TASK_ADAPTER` 决定。

## 禁止事项

- 不允许把题目专有模型放进 `competition_core`。
- 不允许让 `MainWindow` 直接 include 题目目录。
- 不允许新增题目时复制主流程或复制通信客户端。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_task_ports|test_architecture_boundaries|test_main_window"
```

## 推荐提示词

请按 `docs/framework_architecture.md` 审查这次变更属于哪一层，指出任何题目逻辑污染通用框架的风险，并给出最小改造方案。