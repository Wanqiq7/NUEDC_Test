# nuedc-task-adapter

## 触发场景

当用户要求新增电赛题目、题目页面、任务生成流程或 Adapter 时使用。

## 允许修改

- `ground_station_computer/src/h_problem/**`
- `ground_station_computer/src/framework/task/**`
- `ground_station_computer/tests/test_main_window.cpp`
- `ground_station_computer/tests/test_architecture_boundaries.cpp`
- `docs/adding_task_adapter.md`

## 工作规则

- 新题目通过 `CompetitionTaskAdapter` 接入，不修改 `MainWindow` 主流程。
- 必须注册到 `availableCompetitionTaskAdapters()`，并支持 `NUEDC_TASK_ADAPTER=<adapter_id>` 选择。
- 题目 UI、规则校验、规划按钮状态和数据存储都放在题目目录。
- 题目规划输出必须转换成通用 `competition::TaskPlan`。

## 禁止事项

- 不复制 H 题页面后只改文案。
- 不在 framework 通信层解析题目字段。
- 不直接编辑生成的 protobuf 文件。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_main_window|test_mission_load_adapter|test_architecture_boundaries"
```

## 推荐提示词

请为新题目实现一个地面站 Adapter，保持 `MainWindow` 不变，列出新增文件、Adapter 状态流、任务计划转换、工厂注册和测试用例。