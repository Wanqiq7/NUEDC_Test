# Route Display Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将重复路径显示重构为“主航线细蓝线 + 重复航段统一橙色高亮带 + 中央 `Nx` 圆牌”，提升比赛演示时的第一眼可读性。

**Architecture:** 保持现有 Qt6 Widgets、ZeroMQ 和 Protobuf 链路不变，只重构 `RouteVisualizer` 的重复边语义与 `GridScene` 的绘制策略。`MainWindow` 只同步图例与文案，不改变消息流。

**Tech Stack:** C++17, Qt6 Widgets, QtTest, CMake

---

### Task 1: 重构重复边视觉语义

**Files:**
- Modify: `ground_station/src/route_visualizer.h`
- Modify: `ground_station/src/route_visualizer.cpp`
- Test: `ground_station/tests/test_route_visualizer.cpp`

- [ ] **Step 1: 写失败测试，固定新语义**

```cpp
void RouteVisualizerTests::marksRepeatedEdgesWithUniformHighlightMetadata() {
    const QStringList route = {"A1B1", "A2B1", "A1B1", "A2B1"};
    const auto visuals = RouteVisualizer::buildSegmentVisuals(route);

    QCOMPARE(visuals.size(), 3);
    QVERIFY(visuals[0].is_repeated);
    QVERIFY(visuals[1].is_repeated);
    QVERIFY(visuals[2].is_repeated);
    QCOMPARE(visuals[0].repeat_count, 3);
    QCOMPARE(visuals[1].repeat_count, 3);
    QCOMPARE(visuals[2].repeat_count, 3);
    QCOMPARE(visuals[0].color, visuals[1].color);
    QCOMPARE(visuals[1].color, visuals[2].color);
    QCOMPARE(visuals[0].badge_text, QString("3x"));
}
```

- [ ] **Step 2: 运行单测，确认测试先失败**

Run: `QT_QPA_PLATFORM=offscreen /home/qwe/XT/build/ground_station/test_route_visualizer`
Expected: FAIL，提示 `RouteSegmentVisual` 缺少新字段或颜色/语义断言不成立。

- [ ] **Step 3: 写最小实现**

```cpp
struct RouteSegmentVisual {
    QString from_cell;
    QString to_cell;
    bool is_repeated = false;
    int repeat_count = 1;
    QString badge_text;
    QColor color;
};

// buildSegmentVisuals:
// 1. 统计无向边总次数
// 2. total_passes > 1 时标记 is_repeated=true
// 3. 重复边统一返回橙色
// 4. badge_text 使用 QString("%1x").arg(repeat_count)
```

- [ ] **Step 4: 运行单测，确认通过**

Run: `ctest --test-dir /home/qwe/XT/build --output-on-failure -R test_route_visualizer`
Expected: PASS

- [ ] **Step 5: 自检接口一致性**

检查 `GridScene` 是否仍依赖 `pass_index`、`lane_offset` 等旧字段；如果依赖，转入 Task 2 一并收敛，避免残留旧语义。

### Task 2: 改写 GridScene 路径绘制

**Files:**
- Modify: `ground_station/src/grid_scene.cpp`
- Modify: `ground_station/src/grid_scene.h`

- [ ] **Step 1: 先根据新语义写出绘制目标**

```text
普通边:
- 单条细蓝线
- 小箭头

重复边:
- 仍先画普通蓝线骨架
- 再叠加更粗的橙色高亮带
- 在中点放置 `Nx` 圆牌
- 在末端放较大橙色箭头
```

- [ ] **Step 2: 删除旧的平行车道绘制逻辑**

```cpp
// 删除基于 canonical_line + lane_offset 的偏移绘制
// 不再使用 pass_index 控制 zValue、颜色和偏移
QLineF route_line(from_center, to_center);
```

- [ ] **Step 3: 实现 A1 绘制策略**

```cpp
// 先画蓝色主线
// segment_visual.is_repeated 为 true 时：
//   1. 叠加一条更粗的橙色高亮带
//   2. 在 route_line.pointAt(0.5) 附近绘制圆牌
//   3. 圆牌文本来自 segment_visual.badge_text
//   4. 箭头使用橙色并适度放大
```

- [ ] **Step 4: 保持兼容项**

```text
- 非法边仍保留红色虚线
- 起飞区、红球、终点、降落走廊逻辑不变
- tooltip 改为“重复航段 | 共 Nx”
```

- [ ] **Step 5: 构建验证**

Run: `cmake --build /home/qwe/XT/build`
Expected: PASS

### Task 3: 同步主窗口图例与验证联调

**Files:**
- Modify: `ground_station/src/main_window.cpp`

- [ ] **Step 1: 更新图例文案**

```cpp
legend_label_->setText(
    "<b>路线图例</b><br/>"
    "<span style='color:#2f6fed;'>■</span> 主航线"
    "　<span style='color:#f08c00;'>■</span> 重复航段<br/>"
    "<span style='color:#475569;'>圆牌显示重复次数，如 2x / 3x；箭头表示飞行方向。</span>");
```

- [ ] **Step 2: 运行全量测试**

Run: `ctest --test-dir /home/qwe/XT/build --output-on-failure`
Expected: PASS

- [ ] **Step 3: 跑离屏联调**

Run: `QT_QPA_PLATFORM=offscreen ./build/ground_station/ground_station_app`
Run: `PYTHONPATH=python python3 -m uav_testbed.run_simulator --case cases/sample_case.json --sleep-scale 0.02`
Expected: 地面站不崩溃，模拟器正常结束。

- [ ] **Step 4: 检查结果库仍在写入**

Run: 使用 Python `sqlite3` 读取 `ground_station_results.db`
Expected: `detections` 表记录数增加或保持可查询。

- [ ] **Step 5: 记录环境限制**

```text
当前目录不是 Git 仓库，因此本轮不执行 commit，只记录构建、测试、联调结果。
```
