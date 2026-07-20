# nuedc-planner

## 触发场景

当任务涉及航线规划、覆盖率、代价函数、终点选择或边界样例时使用。

## 允许修改

- `shared/cpp/include/h_problem_core/planning/**`
- `shared/cpp/src/planning/**`
- `shared/cpp/tests/test_h_route_planner.cpp`
- `shared/cpp/tests/test_h_planning_result.cpp`
- `web_ground_station/gateway/nuedc_web_gateway/planner.py`

## 工作规则

- 先补 `PlanningResult`、失败原因和基准测试，再改算法。
- 优先保证可解释和稳定，再优化路径长度和耗时。
- 小规模可以精确解，大规模必须有启发式回退。

## 禁止事项

- 不在 UI 层修规划问题。
- 不把规划搜索写成无时间上限的递归。
- 不忽略禁飞区、终点约束和降落可达性。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_h_route_planner|test_h_planning_result"
```

## 推荐提示词

请优化 H 题航线规划器，说明你改了哪一层、代价函数怎样变化、基准案例是否回退，以及失败原因是否更可读。
