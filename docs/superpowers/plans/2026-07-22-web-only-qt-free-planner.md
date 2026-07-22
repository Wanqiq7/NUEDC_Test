# Web-only 地面站与无 Qt 规划器实施计划

> **供智能体执行者使用：** 必须使用子技能：建议使用 superpowers:subagent-driven-development，也可使用 superpowers:executing-plans，按任务逐项实施本计划。步骤使用复选框（`- [ ]`）语法跟踪。

**目标：** 移除所有在用的地面站 Qt 依赖，同时保留现有 C++17 H 题规划器，使其成为无状态 CLI，并使规范化 JSON 行为与 Qt 基线等价。

**架构：** 保持 Vue、FastAPI 和机载 wire 路径不变。冻结当前 Qt CLI 输出，仅用 STL 和 `nlohmann_json` 替换规划器的传递闭包，然后删除无关的 C++ 协议、模拟器、状态与存储代码。在二进制边界以及未修改的 Gateway 工作线程 `subprocess.Popen` 适配器上验证兼容性。

**技术栈：** C++17、CMake 3.16+、STL、nlohmann_json、GoogleTest、Python 3.10/pytest、Vue 3/Vitest/Playwright、uv、pnpm。

## 全局约束

- 唯一的地面端原生组件是无 Qt 的 C++17 无状态航线规划 CLI。
- 保持 `build/shared/cpp/h_route_planner_cli`、`NUEDC_PLANNER_CLI`、`h_planning_request_v1`、响应字段、错误码以及退出码 `0/2/3/4` 不变。
- 保持搜索与航点顺序、重复点、XYZ/action/payload、航线代价、最优性、A9 到 A1 与 B1 到 B7 的坐标轴、下降起点与着陆点的区分，以及 `h_field_m_v1` 不变。
- 不修改 `web_ground_station/gateway/nuedc_web_gateway/planner.py`；保留 `asyncio.to_thread` 加同步 `subprocess.Popen`。
- 保留 Python Protobuf/pyzmq 和 `shared/proto/messages.proto`；仅移除 C++ Protobuf/ZeroMQ 依赖。
- 最终在用源码和 CMake 中不得包含 Qt6、QtTest、AUTOMOC、`Q_OBJECT` 或 `#include <Q...>`。
- 不修改 UI、规划行为、wire 协议、PID/PlotJuggler 范围或机载 ROS 2 代码。
- 在隔离的 Web worktree 中工作，并保留 `/home/sb/Ground_station/AGENTS.md` 中无关的改动。
- 每个实施任务先接受规范符合性审查，再接受代码质量审查。

## 最终文件图

- 创建 `shared/cpp/include/h_problem_core/common/task_plan.h`，存放仅供规划器使用的任务计划值与 JSON 转换。
- 保留并移植 `shared/cpp/{include,src}/h_problem_core/{common,mission,planning,tools}`。
- 保留 `shared/cpp/tools/h_route_planner_cli_main.cpp`，使其仅承担 stdin/stdout 适配。
- 创建 `shared/cases/golden/planner_cli_cases.json`，并在 `shared/cpp/tests/` 下创建捕获/验证脚本。
- 使用 GoogleTest 重写保留的 `shared/cpp/tests/test_h_*.cpp`。
- 创建 `scripts/tests/test_web_only_ground_station.sh`，检查源码/构建边界。
- 删除 `shared/cpp/include/competition_core`、H 模拟器、C++ 协议/存储/任务源码、运行时 fixture 生成器及其测试。

---

### 任务 1：冻结 Qt CLI 基线

**文件：**
- 创建：`shared/cases/golden/planner_cli_cases.json`
- 创建：`shared/cpp/tests/capture_planner_golden.py`
- 创建：`shared/cpp/tests/verify_planner_golden.py`
- 修改：`shared/cpp/CMakeLists.txt`

**接口：**
- 输入：当前 Qt `build/shared/cpp/h_route_planner_cli`。
- 输出：fixture 键 `name`、`stdin_text`、`exit_code`、`response`、`stderr_json_forbidden`；验证器 CLI `verify_planner_golden.py PLANNER FIXTURE`。

- [ ] **步骤 1：编写预期失败的验证器**

从仓库根目录运行每个案例。要求 stdout 恰好包含一个 JSON 文档。递归比较对象；数组按顺序比较；字符串/布尔值精确比较；数字使用 `math.isclose(rel_tol=1e-12, abs_tol=1e-9)`；退出码精确比较；按要求禁止 stderr 中出现 `{`。

- [ ] **步骤 2：验证缺失 fixture 会失败**

运行：`python3 shared/cpp/tests/verify_planner_golden.py build/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json`

预期：由于 fixture 不存在而返回非零。

- [ ] **步骤 3：实现固定基线捕获案例**

按顺序捕获：sample/default barrier；`A2B2/A2B3/A2B4`；原始 `{`；错误 schema；空 case 路径；非数组 no-fly；`A10B1`；缺失 case；起点被阻塞的 `A9B1`。保留原始无效 stdin 和紧凑对象请求；没有 `--overwrite` 时拒绝覆盖；写入稳定缩进的 JSON，并以换行结尾。

- [ ] **步骤 4：构建旧 CLI、捕获并注册 CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target h_route_planner_cli --parallel
python3 shared/cpp/tests/capture_planner_golden.py --planner build/shared/cpp/h_route_planner_cli --output shared/cases/golden/planner_cli_cases.json
ctest --test-dir build -R 'test_h_route_planner_(cli|golden)' --output-on-failure
```

预期：九个 fixture；旧 CLI 和新二进制验证器均通过。

- [ ] **步骤 5：提交**

```bash
git add shared/cases/golden shared/cpp/tests/capture_planner_golden.py shared/cpp/tests/verify_planner_golden.py shared/cpp/CMakeLists.txt
git commit -m "test(planner): freeze Qt CLI golden behavior"
```

---

### 任务 2：迁移最小规划器闭包

**文件：**
- 创建：`shared/cpp/include/h_problem_core/common/task_plan.h`
- 修改：`shared/cpp/include/h_problem_core/{common,mission,planning,tools}` 中保留的头文件
- 修改：`shared/cpp/src/{mission,planning,tools}` 中保留的源码
- 修改：`shared/cpp/tools/h_route_planner_cli_main.cpp`、`shared/cpp/CMakeLists.txt`
- 重写：`test_h_case_loader.cpp`、`test_h_mission_geometry.cpp`、`test_h_route_planner.cpp`、`test_h_planning_result.cpp`、`test_h_route_planner_cli.cpp`

**接口：**
- 输入：任务 1 的 golden fixture 和当前算法主体。
- 输出：`runPlannerCliRequest(std::string_view)`、`loadCase(const std::filesystem::path&, std::string*)`、`buildTaskPlan(const CaseConfig&, std::optional<CellList>, std::string*)`、STL 几何/搜索 API。

- [ ] **步骤 1：使用 GoogleTest 重写保留的测试**

保留每个现有场景，包括精确的小网格最优性和全部合法三格屏障。增加明确断言：`encodeCell(8,0) == "A9B1"`；`encodeCell(0,6) == "A1B7"`；A9B1 映射到任务坐标 `(0,0)`；保留重复点；终点 ID 为 `touchdown`；倒数第二个航点是下降起点单元格；最终航点是独立的 `land`，且 `z=0`。

- [ ] **步骤 2：证明重写后的测试无法针对 Qt API 构建**

运行：`cmake --build build --target test_h_case_loader test_h_mission_geometry test_h_route_planner test_h_planning_result test_h_route_planner_cli --parallel`

预期：因 STL 接口或 GoogleTest 目标缺失而编译失败。

- [ ] **步骤 3：定义精确的规划器值与容器**

使用 `CellList = std::vector<std::string>`、`CellSet = std::set<std::string>`、`GridPoint { int x; int y; }`、定宽序列整数以及仅供规划器使用的 `TaskWaypoint`/`TaskPlan`。不得携带 Ack、命令状态、事件、摘要、互斥量或 Qt 元类型。旧迭代顺序会影响候选项或平局判定时，使用有序 `std::set`/`std::map`；无序容器仅用于纯查找数据。

- [ ] **步骤 4：机械移植几何、代价与航线搜索**

替换 Qt 集合/字符串方法，但不改变循环顺序、比较元组、搜索限制或公式。用固定的 C++17 Pi 常量和局部转换函数替代 Qt 角度/弧度 helper。精确保留着陆候选采样顺序和 epsilon 值。

- [ ] **步骤 5：移植 case 与计划 JSON**

使用 `std::ifstream`、`std::filesystem::path` 和 `nlohmann::json`。转换前验证 JSON 类型，并保留现有英文错误分类。保留默认值 `tick_interval_ms=100`、下降容差 `5.0`、首选航向 `45.0`、航向容差 `35.0`。将 metadata 和 payload 保持为紧凑 JSON 字符串。

- [ ] **步骤 6：移植 CLI 契约**

暴露 `PlannerCliResult { int exit_code; std::string stdout_bytes; std::string stderr_bytes; }`。无效文档/对象/schema/case 路径/no-fly 数组返回 `2`；无效单元格、case 加载失败或无航线返回 `3`；内部 metadata 失败返回 `4`。Main 读取全部 stdin，不向 stdout 添加换行或日志，只向 stderr 写诊断信息，并返回结果码。

- [ ] **步骤 7：构建并验证**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target h_route_planner_cli test_h_case_loader test_h_mission_geometry test_h_route_planner test_h_planning_result test_h_route_planner_cli --parallel
ctest --test-dir build -R 'test_h_(case|mission|route|planning)' --output-on-failure
python3 shared/cpp/tests/verify_planner_golden.py build/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json
ldd build/shared/cpp/h_route_planner_cli | rg 'Qt|protobuf|zmq'
```

预期：测试/golden 通过；最后一个 `rg` 无输出且退出码为 `1`。

- [ ] **步骤 8：提交**

```bash
git add shared/cpp/include/h_problem_core shared/cpp/src/mission shared/cpp/src/planning shared/cpp/src/tools shared/cpp/tools/h_route_planner_cli_main.cpp shared/cpp/tests/test_h_*.cpp shared/cpp/CMakeLists.txt
git commit -m "refactor(planner): remove Qt from route planning CLI"
```

---

### 任务 3：删除 Qt 时代模块并简化 CMake

**文件：**
- 删除：`shared/cpp/include/competition_core`、`shared/cpp/include/h_problem_core/runtime/simulator.h`
- 删除：`shared/cpp/src/{protocol,runtime,storage,task}`
- 删除：`shared/cpp/tools/generate_h_runtime_fixture.cpp`
- 删除：`test_h_simulator.cpp`、`test_json_codec.cpp`、`test_task_ports.cpp`、`test_task_protocol.cpp`
- 创建：`scripts/tests/test_web_only_ground_station.sh`
- 修改：`CMakeLists.txt`、`shared/cpp/CMakeLists.txt`

**接口：**
- 输入：任务 2 的无 Qt 规划器。
- 输出：只包含规划器库/CLI/测试以及现有 shell 测试的构建图。

- [ ] **步骤 1：添加源码边界测试并确认其失败**

扫描在用的顶层 CMake、`shared/cpp`、`web_ground_station` 和 `scripts`，查找 `Qt6|QtTest|CMAKE_AUTOMOC|Q_OBJECT|#include[[:space:]]*<Q`；断言不存在 `ground_station_computer` 和 `shared/cpp/include/competition_core`。

运行：`bash scripts/tests/test_web_only_ground_station.sh`

预期：在当前 CMake/旧文件上失败。

- [ ] **步骤 2：删除旧文件与依赖**

顶层 CMake 保留 C++17、测试和现有 shell 测试；添加 Python3 Interpreter、nlohmann_json、GTest 和 `add_subdirectory(shared/cpp)`。移除 AUTOMOC、Qt、C++ Protobuf 生成、ZeroMQ/cppzmq 和 `proto_messages`。C++ 子目录仅暴露规划器库、CLI、五个 GoogleTest 和 golden 验证器。

- [ ] **步骤 3：在无 Qt 环境下全新构建**

```bash
cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release
cmake --build build-noqt --parallel
ctest --test-dir build-noqt --output-on-failure
bash scripts/tests/test_web_only_ground_station.sh
ldd build-noqt/shared/cpp/h_route_planner_cli | rg 'Qt|protobuf|zmq'
```

预期：构建/测试通过，链接扫描无输出。

- [ ] **步骤 4：提交**

```bash
git add -A CMakeLists.txt shared/cpp scripts/tests/test_web_only_ground_station.sh
git commit -m "build(ground): remove Qt-era native stack"
```

---

### 任务 4：通过未修改的 Gateway 测试真实 CLI

**文件：**
- 修改：`web_ground_station/tests/gateway/test_planner.py`
- 不修改：`web_ground_station/gateway/nuedc_web_gateway/planner.py`

**接口：**
- 输入：`NUEDC_TEST_PLANNER_CLI` 和现有 `PlannerClient`。
- 输出：真实的跨语言集成覆盖。

- [ ] **步骤 1：添加真实二进制 pytest**

仅在变量不存在时跳过；变量存在时要求文件存在。为 sample/default barrier 生成计划，并断言 A9B1、终点 `touchdown`、最终 `land`、`h_field_m_v1` 以及 `metadata.terminal_cell == waypoints[-2].id`。

- [ ] **步骤 2：验证可选模式和启用模式**

```bash
cd web_ground_station
uv run pytest tests/gateway/test_planner.py -q -rs
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest tests/gateway/test_planner.py -q
git diff --exit-code HEAD -- gateway/nuedc_web_gateway/planner.py
```

预期：首次运行有一个明确跳过，第二次运行会执行真实 CLI，生产适配器无 diff。

- [ ] **步骤 3：提交**

```bash
git add web_ground_station/tests/gateway/test_planner.py
git commit -m "test(web): exercise Qt-free planner integration"
```

---

### 任务 5：更新当前文档与 AGENTS.md

**文件：**
- 修改：`README.md`、`shared/cpp/README.md`、`AGENTS.md`、`scripts/tests/test_web_only_ground_station.sh`
- 谨慎修改：`/home/sb/Ground_station/AGENTS.md`

**接口：**
- 输入：最终结构与工作区 ROS 2 指南。
- 输出：不丢失机载指南的 Web-only 构建/测试/职责说明。

- [ ] **步骤 1：使边界测试拒绝过时的当前文档**

仅扫描当前 README 文件和仓库 AGENTS，检查其中是否声称规划器需要 Qt、C++ Protobuf/ZeroMQ、模拟器、命令 handler 或 `competition_core`；排除历史设计记录。

- [ ] **步骤 2：重写仓库文档**

记录 C++ 软件包 `build-essential cmake nlohmann-json3-dev libgtest-dev`；删除旧安装/生成说明。说明 Web 入口 `http://10.42.0.1:8000`、规划器路径、`h_field_m_v1`、golden 验证、真实 Gateway 测试，以及使用 PlotJuggler 进行 PID 调试。

- [ ] **步骤 3：更新两个 AGENTS 文件**

仓库指南使用 STL/nlohmann_json/GoogleTest，并禁止在规划器中加入通信/状态/UI。在编辑前立即重读工作区 AGENTS；仅替换过时的 `NUEDC_Test` Qt 段落，保留所有 ROS 2 指南和无关用户文本，并保留 `shared/proto` 作为 Python Gateway/机载 wire 源。

- [ ] **步骤 4：验证并提交仓库文档**

```bash
bash scripts/tests/test_web_only_ground_station.sh
git diff --check
git add README.md shared/cpp/README.md AGENTS.md scripts/tests/test_web_only_ground_station.sh
git commit -m "docs(ground): document Web-only planner stack"
```

工作区 `/home/sb/Ground_station/AGENTS.md` 仍是此仓库提交之外明确交付的改动。

---

### 任务 6：完整回归与部署检查

**文件：**
- 仅在测试暴露所属组件回归时修改。
- 创建/更新：`.superpowers/sdd/progress.md`

**接口：**
- 输入：所有已迁移代码/文档。
- 输出：精确的原生、Gateway、前端、浏览器、边界和部署证据。

- [ ] **步骤 1：原生与 Gateway 检查**

```bash
cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release
cmake --build build-noqt --parallel
ctest --test-dir build-noqt --output-on-failure
cd web_ground_station
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest -q
uv run ruff check gateway tests scripts
```

- [ ] **步骤 2：前端与浏览器检查**

```bash
cd web_ground_station/frontend
pnpm test
pnpm typecheck
pnpm build
pnpm exec playwright test
```

预期：包括 1024x600 在内的所有已配置视口均通过；因为 UI 未改动，不需要截图审批。

- [ ] **步骤 3：边界/部署检查与证据**

```bash
cd ../..
bash scripts/tests/test_web_only_ground_station.sh
bash web_ground_station/tests/test_scripts.sh
bash web_ground_station/tests/test_cross_repo_manifest_contract.sh
ldd build-noqt/shared/cpp/h_route_planner_cli
```

在 progress 中记录命令/结果和环境跳过项。如果失败，则新增或收紧相应测试，实施最小修复，重跑并进行 scoped commit。绝不提交构建树、缓存、运行时 DB/session 日志、截图或前端 dist。

---

### 任务 7：独立全分支审查与交接

**文件：**
- 审查：`66ce1da..HEAD`
- 仅针对 Critical/Important 发现进行修改。

**接口：**
- 输入：所有任务提交/证据。
- 输出：验收无问题的分支；未经另行请求不合并或推送。

- [ ] **步骤 1：发起独立审查**

审查 golden 等价性、确定性平局判定、精确 CLI 契约、无在用 Qt/C++ wire 依赖、保留 Python wire 代码、未修改 thread/Popen 适配器、坐标轴/重复点/下降/着陆、保留 ROS 2 AGENTS 指南、无生成产物，以及直接与 Gateway 集成覆盖。

- [ ] **步骤 2：修复并复审发现**

为每个 Critical/Important 问题添加能暴露它的测试，实施最小修正，运行聚焦测试，提交并复审，直至没有此类问题。

- [ ] **步骤 3：最终验证与交接**

```bash
git diff --check 66ce1da..HEAD
git status --short
ctest --test-dir build-noqt --output-on-failure
cd web_ground_station
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest tests/gateway/test_planner.py -q
cd frontend
pnpm test && pnpm typecheck && pnpm build
```

报告提交、删除项、移除的依赖、精确测试结果、仅限硬件的剩余验证，以及工作区 AGENTS 改动。未经请求不得合并或推送。
