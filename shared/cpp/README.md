# H 题共享 C++ 规划器

`shared/cpp` 是不依赖 UI 和通信框架的 C++17 规划边界。它提供两个 CMake 目标：

- `h_problem_core`：无状态案例加载、禁飞区校验、路线规划、坐标转换和降落几何库。
- `h_route_planner_cli`：Gateway 调用的 stdin/stdout JSON 进程，构建路径为
  `build/shared/cpp/h_route_planner_cli`。

规划器只使用 STL 与 `nlohmann_json`，测试使用 GoogleTest。它不连接机载端、不处理命令、
不保存运行时权威状态，也不包含桌面或 Web UI。通信、状态镜像、任务持久化及 Protobuf
转换由 Python Gateway 负责；`shared/proto/messages.proto` 是 Python Gateway 与机载端的
wire 源。

## H 题速度任务契约

H 题执行契约为 h_field_m_v1。TaskWaypoint 使用米，A9B1 格心为原点，
+X: B1 -> B7，+Y: A9 -> A1。执行序列为 takeoff -> navigate -> land；
terminal_waypoint_id=touchdown 表示最终落点，metadata_json.terminal_cell
表示最后巡查格。START 由机载 mission_coordinator 直接接受并运行速度闭环，
不再调用 /nuedc/execute_mission Action。

该契约必须原子部署：机载端最终系统测试通过前，不得单独部署这版地面站契约；
地面站与机载端的 LOAD 契约门禁和速度控制器必须在同一部署窗口上线。

## 构建与验证

Ubuntu 22.04 安装 C++ 依赖：

```bash
sudo apt install -y build-essential cmake nlohmann-json3-dev libgtest-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

直接调用 planner：

```bash
printf '%s' '{"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["A4B3","A5B3","A6B3"]}' \
  | build/shared/cpp/h_route_planner_cli
```

固定输出、错误码和退出码由 golden 校验锁定：

```bash
python3 shared/cpp/tests/verify_planner_golden.py \
  build/shared/cpp/h_route_planner_cli \
  shared/cases/golden/planner_cli_cases.json
```

Gateway 的真实进程集成测试：

```bash
cd web_ground_station
NUEDC_TEST_PLANNER_CLI=../build/shared/cpp/h_route_planner_cli \
  uv run pytest tests/gateway/test_planner.py \
  -k real_planner_cli_returns_execution_contract -v
```

## 目录与扩展边界

- `include/h_problem_core/common/`：H 题案例与 `TaskPlan` 数据模型。
- `include/h_problem_core/planning/`：航线、代价与降落几何接口。
- `include/h_problem_core/mission/`：案例加载与 `h_field_m_v1` 计划生成接口。
- `include/h_problem_core/tools/`：版本化 planner CLI 请求/响应接口。
- `src/`：对应实现；`tools/`：可执行程序入口；`tests/`：GoogleTest 与 golden 工具。

只把确定性的 H 题规划逻辑放入这里。通信、状态机、持久化、协议生成和 UI 变更应留在
各自的 Gateway、wire 或前端边界。
