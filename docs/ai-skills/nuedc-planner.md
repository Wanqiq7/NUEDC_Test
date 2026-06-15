# nuedc-planner

## 触发场景

当任务涉及航线规划、覆盖率、路径长度、耗时估算、禁飞区或降落约束时使用。

## 工作规则

- 先写规划测试或基准案例，再改算法。
- 规划器应返回 `PlanningResult`，包括路线、代价、覆盖率、警告和失败原因。
- 优先保证路线合法、可解释和不超时，再优化路线成本。

## 禁止事项

- 不用 UI 层判断路线是否合法。
- 不让精确搜索在大规模案例无限运行。
- 不用隐藏失败原因的空路线作为唯一错误输出。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_h_route_planner|test_h_planning_result"
```

## 推荐提示词

请基于现有 H 题规划器新增一个失败用例或成本回归用例，先证明问题，再优化 `PlanningResult` 输出和路线质量。
