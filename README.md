# H 题混合联调测试平台

这是一个用于验证 2025 年电赛 H 题主链路的混合方案工程：

- Python 机载模拟端
- Qt6 桌面地面站
- ZeroMQ + Protobuf 通信

## 目录

- `proto/`：Protobuf 协议
- `cases/`：固定测试案例
- `python/`：机载模拟端
- `ground_station/`：Qt 地面站

## 构建

```bash
cmake -S . -B build
cmake --build build
```

以上构建会同时生成：

- C++ protobuf 代码到 `build/generated/proto`
- Python protobuf 代码到 `python/uav_testbed/generated`

## 运行 Python 测试

```bash
python3 -m unittest discover -s python/tests -v
```

## 运行 Qt 测试

```bash
ctest --test-dir build --output-on-failure
```

## 启动联调

先启动地面站：

```bash
./build/ground_station/ground_station_app
```

再启动模拟端：

```bash
PYTHONPATH=python python3 -m uav_testbed.run_simulator --case cases/sample_case.json
```

地面站应看到：

- 禁飞区显示为灰色
- 航线覆盖非禁飞区
- 当前巡查方格实时移动
- 动物检测结果滚动追加
- 汇总统计与数据库一致

### 手动设置禁飞区

1. 启动地面站。
2. 点击 `设置禁飞区`。
3. 在左侧地图上点击 3 个横向或纵向连续的格子。
4. 按钮变为 `航线生成` 后点击以生成航线。
5. 地图刷新新的禁飞区与航线。
6. 后续启动模拟端时，会优先读取 `cases/active_mission_plan.json` 中的最新任务计划。
