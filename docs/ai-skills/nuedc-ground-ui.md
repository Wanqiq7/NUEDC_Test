# nuedc-ground-ui

## 触发场景

当任务涉及地面站界面、状态流、按钮可用性、演示效果或误操作防护时使用。

## 允许修改

- `ground_station_computer/src/app/**`
- `ground_station_computer/src/h_problem/ui/**`
- `ground_station_computer/src/framework/runtime/**`
- `ground_station_computer/tests/test_main_window.cpp`
- `ground_station_computer/tests/test_grid_scene.cpp`

## 工作规则

- 主界面只保留比赛现场需要的关键动作。
- 所有按钮状态必须由任务状态机驱动。
- 调试信息默认收纳到次级区域，不占主操作路径。

## 禁止事项

- 不在 UI 里直接写协议细节。
- 不把规划逻辑塞进按钮回调。
- 不为了视觉效果破坏可读性和误操作防护。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure -R "test_main_window|test_grid_scene|test_mission_runtime_state"
```

## 推荐提示词

请按比赛现场操作台标准优化地面站 UI，优先说明状态驱动、按钮约束、信息密度和测试覆盖。