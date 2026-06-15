# AI Skills 索引

这组文件用于约束仓库内的 AI 变更边界。默认优先级如下：

1. 先用 `nuedc-architect` 判断变更层级。
2. 涉及新增题目时用 `nuedc-task-adapter`。
3. 涉及航线、代价函数或边界案例时用 `nuedc-planner`。
4. 涉及地面站界面时用 `nuedc-ground-ui`。
5. 涉及 ZMQ / Protobuf / ROS2 / 任务同步时用 `nuedc-communication`。
6. 交付前用 `nuedc-test-review` 复查。

每个 Skill 都应直接回答四件事：
- 这次变更属于哪一层。
- 允许修改哪些文件。
- 禁止跨越哪些边界。
- 必须运行哪些测试。