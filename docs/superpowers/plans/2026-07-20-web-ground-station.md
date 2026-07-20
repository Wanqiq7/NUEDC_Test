# Web Ground Station Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保留现有 C++ H 题规划器和机载 ZeroMQ/Protobuf 权威协议的前提下，实现可在 `10.42.0.1:8000` 离线运行、支持 `1024x600` 七英寸触摸屏的 FastAPI + Vue 3 Web 地面站。

**Architecture:** 地面 NUC 上的 FastAPI Gateway 通过无状态 C++ CLI 规划，通过 ZeroMQ/Protobuf 与机载端通信，并将规范化 JSON 状态通过 HTTP/WebSocket 提供给单个 Vue 3 主控工作台。Gateway 只维护地面镜像、命令串行化、重试、TTL 和 JSONL；机载端继续是运行态与安全处置权威。

**Tech Stack:** C++17、Qt6 Core、CMake、Protobuf 3.12、ZeroMQ、Python 3.10、FastAPI、Uvicorn、asyncio、pyzmq、uv、Vue 3、Vite、TypeScript、Quasar、Pinia、SVG、pnpm、Pytest、Vitest、Playwright；PID 波形调试继续使用现有 UDP 通道和 PlotJuggler，不纳入 Web 地面站。

## Global Constraints

- 实现工作只在 `/home/sb/Ground_station/.worktrees/nuedc-web` 的 `feat/web-ground-station` 分支进行。
- 第一版只实现 `h_problem`，但通用 Gateway、状态和 Shell 不得包含 H 题规划规则。
- 不修改机载协议和 `shared/proto/messages.proto`；动态字段继续使用既有 `payload_json` / `metadata_json`。
- C++ 规划器是唯一规划权威；Python 和 TypeScript 禁止复制路径规划、坐标转换和降落几何。
- 命令走 HTTP，连续状态走 WebSocket；浏览器不直接访问 ZeroMQ。
- 命令链路和遥测链路分别计算健康状态；telemetry 不得建立命令在线状态。
- 浏览器断开、刷新或 Gateway 重启不自动 STOP；飞行安全语义沿用原工程。
- 生产模式不使用热重载，不依赖 CDN 或互联网；PID 波形调试由 PlotJuggler 负责，不在 Web 地面站中重复实现。
- 前端核心类型保持 `strict: true`，但 Vite 开发服务器不运行阻塞式类型检查。
- 只提交 `pnpm-lock.yaml` 和 `uv.lock`，不提交 npm/Yarn/Poetry 的第二套 lockfile。
- 完整主控必须支持 Chromium 的 `1920x1080`、`1366x768`、`1024x600`。
- Qt 地面站迁移期保留；Qt 与 Web Gateway 不得同时发送控制命令。
- 新增文档使用中文，代码标识符使用英文。

## Planned File Map

```text
shared/cpp/
├── include/h_problem_core/tools/planner_cli.h
├── src/tools/planner_cli.cpp
├── tools/h_route_planner_cli_main.cpp
└── tests/test_h_route_planner_cli.cpp

web_ground_station/
├── pyproject.toml
├── uv.lock
├── gateway/nuedc_web_gateway/
│   ├── __init__.py
│   ├── app.py                 # FastAPI app/lifespan/routes/static files
│   ├── airborne.py            # ZeroMQ/Protobuf transport and loops
│   ├── config.py              # Environment configuration
│   ├── models.py              # Web request/snapshot/event models
│   ├── planner.py             # C++ CLI subprocess adapter
│   ├── proto_runtime.py       # Load generated messages_pb2
│   ├── recorder.py            # Bounded JSONL writer
│   ├── state.py               # Authoritative ground-side mirror/broadcast
│   └── emergency_stop.py      # Browser-independent STOP CLI
├── frontend/
│   ├── package.json / pnpm-lock.yaml / vite.config.ts / tsconfig*.json
│   └── src/
│       ├── App.vue
│       ├── main.ts
│       ├── api/gateway.ts
│       ├── models/gateway.ts
│       ├── stores/ground.ts
│       ├── composables/useTelemetry.ts
│       ├── components/HMissionMap.vue
│       ├── panels/PlanningPanel.vue
│       ├── panels/RuntimePanel.vue
│       ├── panels/DetectionPanel.vue
│       └── styles/app.scss
├── scripts/
│   ├── generate_python_proto.py
│   ├── start_dev.sh
│   ├── start_competition.sh
│   └── check_web_ground_station.sh
└── tests/
    ├── gateway/
    └── e2e/

runtime/
├── web_ground_station.env
├── web_ground_station.env.example
├── active_mission_plan.json      # Generated at runtime; remains ignored
└── sessions/                     # Generated JSONL session logs; remains ignored
```

---

### Task 1: Add the Stateless C++ Planning CLI

**Files:**
- Create: `shared/cpp/include/h_problem_core/tools/planner_cli.h`
- Create: `shared/cpp/src/tools/planner_cli.cpp`
- Create: `shared/cpp/tools/h_route_planner_cli_main.cpp`
- Create: `shared/cpp/tests/test_h_route_planner_cli.cpp`
- Modify: `shared/cpp/CMakeLists.txt`

**Interfaces:**
- Consumes: `hcore::loadCase()`, `hcore::buildTaskPlan()`, `competition::taskPlanToJson()`.
- Produces: `hcore::PlannerCliResult runPlannerCliRequest(const QByteArray &request_bytes)` and executable target `h_route_planner_cli`.
- JSON request schema: `h_planning_request_v1`; exit codes `0`, `2`, `3`, `4` as fixed in the design.

- [ ] **Step 1: Write the failing CLI contract test**

```cpp
#include <QtTest/QtTest>
#include <QJsonDocument>
#include "h_problem_core/tools/planner_cli.h"

class HRoutePlannerCliTests : public QObject {
    Q_OBJECT
private slots:
    void plansCanonicalTask();
    void rejectsUnknownSchema();
    void rejectsInvalidNoFlyCell();
    void rejectsMissingCase();
};

void HRoutePlannerCliTests::plansCanonicalTask() {
    const QByteArray input = R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["A2B2","A2B3","A2B4"]})";
    const hcore::PlannerCliResult result = hcore::runPlannerCliRequest(input);
    QCOMPARE(result.exit_code, 0);
    const QJsonObject output = QJsonDocument::fromJson(result.stdout_bytes).object();
    QVERIFY(output.value("ok").toBool());
    QCOMPARE(output.value("plan").toObject().value("message_type").toString(), QString("task_plan"));
    QCOMPARE(output.value("plan").toObject().value("terminal_waypoint_id").toString(), QString("touchdown"));
}

void HRoutePlannerCliTests::rejectsUnknownSchema() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"wrong","case_path":"shared/cases/sample_case.json","no_fly_cells":[]})");
    QCOMPARE(result.exit_code, 2);
    QCOMPARE(QJsonDocument::fromJson(result.stdout_bytes).object().value("error_code").toString(), QString("invalid_request"));
}

void HRoutePlannerCliTests::rejectsInvalidNoFlyCell() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["invalid"]})");
    QCOMPARE(result.exit_code, 3);
    QCOMPARE(QJsonDocument::fromJson(result.stdout_bytes).object().value("ok").toBool(), false);
}

void HRoutePlannerCliTests::rejectsMissingCase() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/missing.json","no_fly_cells":[]})");
    QCOMPARE(result.exit_code, 3);
    const QJsonDocument document = QJsonDocument::fromJson(result.stdout_bytes);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value("error_code").toString(), QString("case_load_failed"));
    QVERIFY(result.stderr_bytes.isEmpty() || !result.stderr_bytes.contains('{'));
}

QTEST_MAIN(HRoutePlannerCliTests)
#include "test_h_route_planner_cli.moc"
```

- [ ] **Step 2: Register the missing test and verify RED**

Add to `shared/cpp/CMakeLists.txt`:

```cmake
add_h_problem_core_test(test_h_route_planner_cli tests/test_h_route_planner_cli.cpp)
```

Run: `cmake --build build --target test_h_route_planner_cli --parallel 2`

Expected: FAIL because `h_problem_core/tools/planner_cli.h` does not exist.

- [ ] **Step 3: Implement the request function and executable**

Create the public interface:

```cpp
#pragma once
#include <QByteArray>

namespace hcore {
struct PlannerCliResult {
    int exit_code = 4;
    QByteArray stdout_bytes;
    QByteArray stderr_bytes;
};
PlannerCliResult runPlannerCliRequest(const QByteArray &request_bytes);
}
```

Implement `runPlannerCliRequest()` with `QJsonDocument`, require exact schema/case path/string array,
call `loadCase()` and `buildTaskPlan(case_config, no_fly_cells, &error)`, and return compact JSON:

```cpp
QJsonObject success{{"ok", true}, {"plan", competition::taskPlanToJson(plan)}};
const QJsonObject metadata = QJsonDocument::fromJson(plan.metadata_json.toUtf8()).object();
success["metrics"] = QJsonObject{
    {"estimated_mission_time_s", metadata.value("estimated_mission_time_s")},
    {"planning_optimality", metadata.value("planning_optimality")},
};
```

The `main()` must read all stdin, call the function, write only `stdout_bytes` to stdout,
`stderr_bytes` to stderr, and return `exit_code`.

Update CMake:

```cmake
list(APPEND H_PROBLEM_CORE_SOURCES src/tools/planner_cli.cpp)
add_executable(h_route_planner_cli tools/h_route_planner_cli_main.cpp)
target_link_libraries(h_route_planner_cli PRIVATE Qt6::Core h_problem_core competition_core)
```

- [ ] **Step 4: Verify unit and process contracts**

Run:

```bash
cmake --build build --target h_route_planner_cli test_h_route_planner_cli --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R test_h_route_planner_cli --output-on-failure
printf '%s' '{"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["A2B2","A2B3","A2B4"]}' | build/shared/cpp/h_route_planner_cli
```

Expected: test PASS; command exits `0` and stdout is one JSON object with `ok:true` and canonical plan.

- [ ] **Step 5: Commit**

```bash
git add shared/cpp
git commit -m "feat(planner): add stateless H route CLI"
```

### Task 2: Establish the Python Project, Configuration, and Generated Protobuf Boundary

**Files:**
- Create: `web_ground_station/pyproject.toml`
- Create: `web_ground_station/uv.lock` (generated)
- Create: `web_ground_station/gateway/nuedc_web_gateway/__init__.py`
- Create: `web_ground_station/gateway/nuedc_web_gateway/config.py`
- Create: `web_ground_station/gateway/nuedc_web_gateway/proto_runtime.py`
- Create: `web_ground_station/scripts/generate_python_proto.py`
- Create: `web_ground_station/tests/gateway/test_config.py`
- Create: `web_ground_station/tests/gateway/test_proto_runtime.py`
- Modify: `.gitignore`

**Interfaces:**
- Produces: immutable `GatewayConfig(airborne_host: str, telemetry_port: int,
  command_port: int, pid_debug_enabled: bool, pid_debug_port: int, web_host: str, web_port: int,
  runtime_dir: Path, planner_cli: Path)` and
  `GatewayConfig.from_env(env: Mapping[str, str]) -> GatewayConfig`.
- Produces: `GatewayConfig.telemetry_endpoint: str` and `command_endpoint: str` properties.
- Produces: `load_messages_module() -> ModuleType`, generated only from `shared/proto/messages.proto`.

- [ ] **Step 1: Write failing configuration and proto tests**

```python
def test_competition_defaults():
    config = GatewayConfig.from_env({})
    assert config.airborne_host == "10.42.0.2"
    assert config.telemetry_endpoint == "tcp://10.42.0.2:5557"
    assert config.command_endpoint == "tcp://10.42.0.2:5558"
    assert config.web_host == "0.0.0.0"
    assert config.web_port == 8000
    assert config.pid_debug_enabled is False

def test_loads_generated_envelope():
    messages = load_messages_module()
    envelope = messages.Envelope(sequence=7)
    assert envelope.sequence == 7
```

- [ ] **Step 2: Run tests to verify RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_config.py tests/gateway/test_proto_runtime.py -v`

Expected: FAIL because the package and uv project do not exist.

- [ ] **Step 3: Create the uv project and focused config type**

Use this project metadata and resolve exact versions into `uv.lock`:

```toml
[project]
name = "nuedc-web-ground-station"
version = "0.1.0"
requires-python = ">=3.10,<3.11"
dependencies = [
  "fastapi>=0.115,<1",
  "pydantic>=2.10,<3",
  "uvicorn>=0.30,<1",
  "pyzmq>=22,<27",
  "protobuf>=3.20,<4",
]

[dependency-groups]
dev = ["httpx>=0.27,<1", "pytest>=8,<9", "pytest-asyncio>=0.24,<1", "ruff>=0.8,<1"]

[tool.pytest.ini_options]
pythonpath = ["gateway"]
asyncio_mode = "auto"
```

`GatewayConfig` is a frozen dataclass with typed host/port/path fields. Parse booleans only from
`1/true/yes/on`, validate ports `1..65535`, and derive the two ZeroMQ endpoints as properties.

Before the first uv command, install the project-pinned tool once and verify it:

```bash
python3 -m pip install --user uv==0.8.14
uv --version
```

Expected: `uv 0.8.14`.

- [ ] **Step 4: Implement deterministic proto generation/loading**

`generate_python_proto.py` must locate repo root from its own path, run system `protoc` into
`web_ground_station/.generated/proto`, and create `__init__.py`. `proto_runtime.py` checks mtime,
regenerates when stale, and imports `messages_pb2` via `importlib.util`. Add:

```gitignore
/web_ground_station/.generated/
/web_ground_station/.venv/
/web_ground_station/frontend/node_modules/
/web_ground_station/frontend/dist/
/runtime/sessions/
```

- [ ] **Step 5: Lock dependencies and verify GREEN**

Run:

```bash
cd web_ground_station
uv lock
uv sync --frozen
uv run pytest tests/gateway/test_config.py tests/gateway/test_proto_runtime.py -v
uv run ruff check gateway tests/gateway
```

Expected: all tests PASS; Ruff reports no errors; `.generated` remains untracked.

- [ ] **Step 6: Commit**

```bash
git add .gitignore web_ground_station
git commit -m "build(web): establish gateway toolchain"
```

### Task 3: Add the Async Planner Adapter and Atomic Plan Store

**Files:**
- Create: `web_ground_station/gateway/nuedc_web_gateway/planner.py`
- Create: `web_ground_station/gateway/nuedc_web_gateway/models.py`
- Create: `web_ground_station/tests/gateway/test_planner.py`

**Interfaces:**
- Consumes: C++ `h_route_planner_cli` stdin/stdout contract from Task 1.
- Produces: frozen dataclass `PlanningRequest(case_path: str, no_fly_cells: list[str])`.
- Produces: `PlannerClient.plan(request: PlanningRequest) -> dict[str, Any]`.
- Produces: `store_plan_atomic(plan: Mapping[str, Any], output_path: Path) -> None`.

- [ ] **Step 1: Write failing async tests with a fake executable**

```python
@pytest.mark.asyncio
async def test_returns_plan_and_stores_atomically(tmp_path):
    executable = make_executable(tmp_path, '#!/bin/sh\nprintf \'%s\' \'{"ok":true,"plan":{"message_type":"task_plan","task_id":"case-1","waypoints":[{"id":"A9B1"}]},"metrics":{}}\'\n')
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)
    plan = await client.plan(PlanningRequest(case_path="shared/cases/sample_case.json", no_fly_cells=[]))
    output = tmp_path / "active.json"
    store_plan_atomic(plan, output)
    assert json.loads(output.read_text())["task_id"] == "case-1"

@pytest.mark.asyncio
async def test_times_out_without_replacing_plan(tmp_path):
    executable = make_executable(tmp_path, "#!/bin/sh\nsleep 2\n")
    client = PlannerClient(executable, timeout_s=0.01, max_output_bytes=4096)
    with pytest.raises(PlannerError, match="timeout"):
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))

@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("script", "error_code"),
    [
        ("#!/bin/sh\nexit 4\n", "planner_process_failed"),
        ("#!/bin/sh\nprintf 'not-json'\n", "planner_invalid_response"),
        ("#!/bin/sh\nprintf '%05000d' 0\n", "planner_output_too_large"),
    ],
)
async def test_rejects_failed_or_invalid_planner_output(tmp_path, script, error_code):
    executable = make_executable(tmp_path, script)
    client = PlannerClient(executable, timeout_s=1.0, max_output_bytes=4096)
    with pytest.raises(PlannerError) as caught:
        await client.plan(PlanningRequest(case_path="case.json", no_fly_cells=[]))
    assert caught.value.error_code == error_code
```

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_planner.py -v`

Expected: FAIL because `PlannerClient` and models are missing.

- [ ] **Step 3: Implement bounded subprocess execution**

`PlannerClient.plan()` must serialize exactly:

```python
payload = {
    "schema": "h_planning_request_v1",
    "case_path": request.case_path,
    "no_fly_cells": request.no_fly_cells,
}
```

Use `asyncio.create_subprocess_exec`, reject serialized stdin above 64 KiB before spawning,
`communicate(input=request_bytes)`, `asyncio.wait_for`, kill/wait on timeout, reject oversized stdout,
reject non-object JSON, reject `ok != true`, and require
`message_type == "task_plan"`, non-empty `task_id`, and non-empty waypoint list.
`store_plan_atomic()` writes compact UTF-8 JSON to a sibling temporary file, flushes and
`os.replace()`s it.

- [ ] **Step 4: Run GREEN**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_planner.py -v`

Expected: all planner adapter tests PASS.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/gateway/nuedc_web_gateway web_ground_station/tests/gateway/test_planner.py
git commit -m "feat(web): bridge the C++ planner"
```

### Task 4: Implement the Ground State Mirror and Bounded JSONL Recorder

**Files:**
- Modify: `web_ground_station/gateway/nuedc_web_gateway/models.py`
- Create: `web_ground_station/gateway/nuedc_web_gateway/state.py`
- Create: `web_ground_station/gateway/nuedc_web_gateway/recorder.py`
- Create: `web_ground_station/tests/gateway/test_state.py`
- Create: `web_ground_station/tests/gateway/test_recorder.py`

**Interfaces:**
- Produces: `WebEvent(schema: Literal["nuedc.web.v1"], type: str, seq: int,
  timestamp_ms: int, task_id: str | None, event: str | None, payload: dict[str, Any])`.
- Produces: `AckSnapshot(ok: bool, message: str, task_id: str, mission_loaded: bool,
  mission_running: bool, last_accepted_sequence: int, vision_armed: bool)`.
- Produces: `GroundSnapshot` and `CommandResponse` Pydantic models using snake_case wire fields.
- Produces: `GroundState.snapshot(now_ms: int) -> GroundSnapshot`.
- Produces: `GroundState.apply_ack(ack: AckSnapshot, timestamp_ms: int) -> None`.
- Produces: `GroundState.apply_plan(plan: Mapping[str, Any], timestamp_ms: int) -> None`.
- Produces: `GroundState.apply_task_event(task_id: str, event: str, seq: int,
  timestamp_ms: int, payload: Mapping[str, Any]) -> None`.
- Produces: `GroundState.apply_summary(task_id: str, seq: int, timestamp_ms: int,
  success: bool, payload: Mapping[str, Any]) -> None`.
- Produces: `GroundState.subscribe(maxsize: int = 64) -> asyncio.Queue[WebEvent]`.
- Produces: `JsonlRecorder.start() -> Awaitable[None]`,
  `record(event: WebEvent) -> bool`, `stop() -> Awaitable[None]`.

- [ ] **Step 1: Write failing state isolation and backpressure tests**

```python
def test_other_task_event_does_not_change_snapshot():
    state = GroundState()
    state.apply_plan({"task_id": "active", "message_type": "task_plan", "waypoints": [{"id": "A9B1"}]}, 100)
    state.apply_task_event(task_id="old", event="telemetry", seq=2, timestamp_ms=101,
                           payload={"current_cell": "A1B1"})
    assert state.snapshot(102).current_cell is None

def test_latest_telemetry_replaces_intermediate_frame():
    state = GroundState()
    state.apply_plan({"task_id": "active", "message_type": "task_plan", "waypoints": [{"id": "A9B1"}]}, 100)
    state.apply_task_event("active", "telemetry", 1, 101, {"current_cell": "A8B1"})
    state.apply_task_event("active", "telemetry", 2, 102, {"current_cell": "A7B1"})
    assert state.snapshot(103).current_cell == "A7B1"

def test_detection_deduplicates_track_and_updates_totals():
    state = active_state("active")
    payload = {"track_id": "track-7", "cell_code": "A7B1", "animal_name": "wolf", "count": 1}
    state.apply_task_event("active", "detection", 3, 103, payload)
    state.apply_task_event("active", "detection", 4, 104, payload)
    snapshot = state.snapshot(105)
    assert snapshot.detection_totals == {"wolf": 1}
    assert len(snapshot.recent_detections) == 1

def test_repeated_or_older_sequence_cannot_overwrite_current_state():
    state = active_state("active")
    state.apply_task_event("active", "target_update", 9, 109, {"target_cell": "A5B2"})
    state.apply_task_event("active", "target_update", 9, 110, {"target_cell": "A1B1"})
    state.apply_task_event("active", "target_update", 8, 111, {"target_cell": "A2B1"})
    assert state.snapshot(112).target_update == {"target_cell": "A5B2"}

def test_ten_thousand_telemetry_frames_keep_bounded_state_and_subscriber_queue():
    state = active_state("active")
    subscriber = state.subscribe(maxsize=64)
    for seq in range(1, 10_001):
        state.apply_task_event("active", "telemetry", seq, 100 + seq,
                               {"current_cell": "A7B1", "visited_cells": seq})
    assert subscriber.qsize() <= 64
    assert state.snapshot(10_101).visited_count == 10_000
```

Recorder test enqueues three events, stops, reads the session file, and asserts three valid JSON
lines in sequence. A full queue test asserts `record()` never blocks and increments `dropped_logs`.
A rotation test sets `max_file_bytes=128`, writes enough events to exceed it, and asserts numbered
session parts are each valid JSONL and preserve event sequence across file boundaries.

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_state.py tests/gateway/test_recorder.py -v`

Expected: FAIL because state/recorder do not exist.

- [ ] **Step 3: Implement one lock-protected mirror and non-blocking fan-out**

Keep one active task and these exact link constants:

```python
COMMAND_HEARTBEAT_MS = 2000
COMMAND_FAILURES_OFFLINE = 3
TELEMETRY_TTL_MS = 5000
PID_TTL_MS = 500
```

`snapshot()` returns current plan, link states, ACK mirror, running state, current cell, visited
count, detection totals, recent detections, target update, recent summary/error, and monotonically
increasing `snapshot_seq`. Detection dedupe key is `(task_id, track_id)`; when `track_id` is absent,
use the event sequence so distinct legacy detections remain distinct. Track the highest applied
Envelope/WebEvent sequence per active task; repeated or older sequence values are ignored and logged
at debug level before any mirror or detection mutation.
Subscribers use bounded queues; when a high-frequency update queue is full, remove its oldest
high-frequency item and enqueue the newest. Do not discard summary/error state before applying it
to the mirror.

`JsonlRecorder` uses a bounded `asyncio.Queue`, one writer task, compact JSON, session filename in
UTC, numbered size-based rotation, and explicit flush on `stop()`. A write/open failure updates
`GroundState.recording_error` and drops later log writes without stopping command processing.

- [ ] **Step 4: Run GREEN**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_state.py tests/gateway/test_recorder.py -v`

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/gateway web_ground_station/tests/gateway
git commit -m "feat(web): model gateway state and session logs"
```

### Task 5: Port the Reliable ZeroMQ Command and Telemetry Adapters

**Files:**
- Create: `web_ground_station/gateway/nuedc_web_gateway/airborne.py`
- Create: `web_ground_station/tests/gateway/test_airborne.py`

**Interfaces:**
- Consumes: generated `messages_pb2`, `GroundState`, `JsonlRecorder`.
- Produces: `GroundControlCommand(str, Enum)` with `START`, `STOP`, `PING`, `ARM_TARGETING`,
  `DISARM_TARGETING`, mapped explicitly to the existing Protobuf `COMMAND_TYPE_*` constants.
- Produces: `AirborneClient.send_mission_load(plan) -> AckSnapshot`.
- Produces: `AirborneClient.send_control(command: GroundControlCommand, task_id: str) -> AckSnapshot`.
- Produces: `AirborneClient.telemetry_loop(stop: asyncio.Event)` and `heartbeat_loop(stop)`.

- [ ] **Step 1: Write failing transport tests with in-process ZeroMQ sockets**

Tests use an in-process REP/PUB fixture with reserved loopback ports and cover:

```python
@pytest.mark.asyncio
async def test_stale_ack_confirming_sequence_is_success(airborne_client, rep_server):
    rep_server.queue_ack(success=False, message="stale command", last_accepted_sequence=2**62)
    ack = await airborne_client.send_control(GroundControlCommand.PING, "case-1")
    assert ack.ok is True
    assert ack.message == "command already accepted"

@pytest.mark.asyncio
async def test_commands_are_physically_serialized(airborne_client, rep_server):
    rep_server.block_replies()
    first = asyncio.create_task(airborne_client.send_control(GroundControlCommand.PING, "case-1"))
    second = asyncio.create_task(airborne_client.send_control(GroundControlCommand.PING, "case-1"))
    await rep_server.wait_for_request_count(1)
    assert rep_server.max_simultaneous_requests == 1
    rep_server.release_replies()
    await asyncio.gather(first, second)

@pytest.mark.asyncio
async def test_three_ping_failures_mark_command_offline(airborne_client, ground_state):
    airborne_client.replace_transport(AlwaysTimeoutTransport())
    for _ in range(3):
        await airborne_client.probe_once()
    assert ground_state.snapshot(now_ms()).command_link == "offline"

@pytest.mark.asyncio
async def test_telemetry_never_marks_command_online(airborne_client, ground_state, pub_server):
    await pub_server.publish(task_event("case-1", "telemetry", 1, {"current_cell": "A8B1"}))
    await airborne_client.receive_one_telemetry()
    assert ground_state.snapshot(now_ms()).command_link != "online"

@pytest.mark.asyncio
async def test_other_task_event_is_ignored(airborne_client, ground_state, pub_server):
    ground_state.apply_plan(plan_for("case-1"), now_ms())
    await pub_server.publish(task_event("old", "telemetry", 1, {"current_cell": "A1B1"}))
    await airborne_client.receive_one_telemetry()
    assert ground_state.snapshot(now_ms()).current_cell is None

@pytest.mark.asyncio
async def test_summary_updates_running_false(airborne_client, ground_state, pub_server):
    ground_state.apply_ack(running_ack("case-1"), now_ms())
    await pub_server.publish(task_summary("case-1", success=True))
    await airborne_client.receive_one_telemetry()
    assert ground_state.snapshot(now_ms()).mission_running is False
```

The stale ACK fixture returns `success=false`, `message="stale command"`, and
`last_accepted_sequence >= sent sequence`; expected result is accepted with message
`command already accepted`.

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_airborne.py -v`

Expected: FAIL because `AirborneClient` is missing.

- [ ] **Step 3: Implement protocol conversion and reliable requests**

Use `zmq.asyncio.Context`, one `asyncio.Lock`, 1500 ms send/receive timeout, three attempts, and
120 ms retry delay. Create a fresh REQ socket per physical attempt so a timed-out REQ socket cannot
remain in an invalid state. Seed sequence as `(time.time_ns() // 1_000_000) << 20` and increment.

Implement exact builders for `mission_load` and `control_command`; convert canonical plan dicts
into Protobuf without changing JSON strings. Parse ACK fields into `AckSnapshot`. SUB loop parses
TaskPlan/TaskEvent/TaskSummary, uses `Envelope.sequence` as `WebEvent.seq`, preserves the waypoint
progress index inside payload, decodes `payload_json` as an object, normalizes to `WebEvent`, then
applies state and recorder before broadcasting.

- [ ] **Step 4: Run GREEN, including socket-permission tests**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_airborne.py -v`

Expected: all tests PASS. If the sandbox rejects loopback socket creation, rerun this exact test
with local-network permission; do not weaken assertions.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/gateway/nuedc_web_gateway/airborne.py web_ground_station/tests/gateway/test_airborne.py
git commit -m "feat(web): connect the reliable airborne link"
```

### Task 6: Expose FastAPI Planning, Command, Snapshot, and WebSocket APIs

**Files:**
- Create: `web_ground_station/gateway/nuedc_web_gateway/app.py`
- Create: `web_ground_station/tests/gateway/test_app.py`

**Interfaces:**
- Consumes: `GatewayConfig`, `PlannerClient`, `GroundState`, `JsonlRecorder`, `AirborneClient`.
- Produces: `GatewayServices(config: GatewayConfig, planner: PlannerClient, state: GroundState,
  recorder: JsonlRecorder, airborne: AirborneClient)`.
- Produces: `create_app(services: GatewayServices | None = None) -> FastAPI` and routes from design.

- [ ] **Step 1: Write failing HTTP and WebSocket tests with fake services**

```python
def test_start_returns_ack_without_optimistic_state(client, fake_airborne):
    fake_airborne.next_ack = AckSnapshot(ok=True, message="start accepted", task_id="case-1",
        mission_loaded=True, mission_running=True, last_accepted_sequence=9, vision_armed=True)
    response = client.post("/api/mission/start")
    assert response.status_code == 200
    assert response.json()["ack"]["mission_running"] is True

def test_plan_failure_keeps_previous_plan(client, fake_planner, runtime_path):
    previous = runtime_path.read_text()
    fake_planner.error = PlannerError("planner timeout", "planner_timeout")
    response = client.post("/api/mission/plan", json={"case_path":"shared/cases/sample_case.json","no_fly_cells":[]})
    assert response.status_code == 504
    assert runtime_path.read_text() == previous

def test_websocket_sends_snapshot_before_incremental_event(client):
    with client.websocket_connect("/ws/telemetry") as socket:
        assert socket.receive_json()["type"] == "snapshot"

def test_start_is_rejected_until_airborne_confirms_loaded(client, fake_state):
    fake_state.set_snapshot(command_link="online", mission_loaded=False, mission_running=False)
    response = client.post("/api/mission/start")
    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_not_ready"

def test_start_rejects_ack_for_a_different_task(client, fake_state):
    fake_state.set_snapshot(command_link="online", active_task_id="case-1",
                            ack={"task_id": "old", "mission_loaded": True},
                            mission_loaded=True, mission_running=False)
    response = client.post("/api/mission/start")
    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_not_ready"

def test_stop_is_rejected_when_mission_is_not_running(client, fake_state):
    fake_state.set_snapshot(command_link="online", mission_loaded=True, mission_running=False)
    response = client.post("/api/mission/stop")
    assert response.status_code == 409
    assert response.json()["error_code"] == "mission_not_running"

def test_restart_restores_plan_but_not_flight_state(tmp_path, fake_airborne):
    write_plan(tmp_path / "active_mission_plan.json", plan_for("case-1"))
    services = build_services(runtime_dir=tmp_path, airborne=fake_airborne)
    snapshot = services.state.snapshot(now_ms=100)
    assert snapshot.active_task_id == "case-1"
    assert snapshot.mission_loaded is False
    assert snapshot.mission_running is False
    assert snapshot.command_link == "resyncing"

def test_lifespan_probes_before_start_can_be_enabled(client, fake_airborne):
    assert fake_airborne.probe_count == 1
    assert client.post("/api/mission/start").status_code == 409
    fake_airborne.complete_probe(running_ack("case-1"))
    assert client.get("/api/snapshot").json()["command_link"] == "online"
```

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_app.py -v`

Expected: FAIL because app factory/routes are missing.

- [ ] **Step 3: Implement lifespan-owned services and exact endpoints**

`GatewayServices` owns state, recorder, planner, airborne, stop event, and background tasks. At
construction it may load `runtime/active_mission_plan.json` for display only; it always initializes
loaded/running/link state as false/resyncing. Lifespan starts recorder and immediately probes the
airborne endpoint before the periodic heartbeat; only a valid ACK may rebuild loaded/running state
and enable START. Shutdown sets stop, awaits/cancels tasks, closes sockets, and flushes recorder.

Implement:

```text
GET  /api/health
GET  /api/snapshot
POST /api/mission/plan
POST /api/mission/load
POST /api/mission/start
POST /api/mission/stop
POST /api/link/probe
WS   /ws/telemetry
```

Map invalid requests to 422, planner rejection to 409, planner timeout to 504, and internal planner
failure to 502. Do not mutate running state before ACK. Mount `frontend/dist` at `/` only when it
exists, and register API/WS routes before the static mount.

- [ ] **Step 4: Run GREEN**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_app.py -v`

Expected: all route/lifespan/WebSocket tests PASS.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/gateway/nuedc_web_gateway/app.py web_ground_station/tests/gateway/test_app.py
git commit -m "feat(web): expose the ground station API"
```

### Task 7: Scaffold the Strict, Non-Blocking Vue/Quasar Frontend and Ground Store

**Files:**
- Create: `web_ground_station/frontend/package.json`
- Create: `web_ground_station/frontend/pnpm-lock.yaml` (generated)
- Create: `web_ground_station/frontend/index.html`
- Create: `web_ground_station/frontend/vite.config.ts`
- Create: `web_ground_station/frontend/tsconfig.json`
- Create: `web_ground_station/frontend/tsconfig.app.json`
- Create: `web_ground_station/frontend/src/main.ts`
- Create: `web_ground_station/frontend/src/App.vue`
- Create: `web_ground_station/frontend/src/models/gateway.ts`
- Create: `web_ground_station/frontend/src/api/gateway.ts`
- Create: `web_ground_station/frontend/src/stores/ground.ts`
- Create: `web_ground_station/frontend/src/stores/ground.spec.ts`
- Create: `web_ground_station/frontend/src/composables/useTelemetry.ts`

**Interfaces:**
- Consumes: `/api/snapshot`, command HTTP responses, `/ws/telemetry` Web JSON.
- Produces: `useGroundStore()` with `applySnapshot`, `applyEvent`, `planMission`, `loadMission`,
  `startMission`, `stopMission`, `probeLink`.

- [ ] **Step 1: Create package metadata and write failing store tests**

Pin compatible major versions and lock them with pnpm: Vue 3, Quasar 2, Pinia 2,
Vite 5, TypeScript 5.5, Vitest 2, Vue Test Utils 2, jsdom 24, Playwright 1,
`@quasar/vite-plugin` 1, `@quasar/extras` 1 and Sass 1. Icons/fonts are imported from the bundled
`@quasar/extras` package; no HTML or CSS may reference a CDN. Set `packageManager` to
`pnpm@10.15.0` and bootstrap it with:

```bash
corepack prepare pnpm@10.15.0 --activate
pnpm --version
```

Expected: `10.15.0`.

```ts
it('does not accept telemetry for another task', () => {
  const store = useGroundStore();
  store.applySnapshot(snapshot({ active_task_id: 'case-1', current_cell: null }));
  store.applyEvent(event({ task_id: 'old', event: 'telemetry', payload: { current_cell: 'A1B1' } }));
  expect(store.currentCell).toBeNull();
});

it('does not optimistically mark a mission running', async () => {
  mockPost('/api/mission/start', Promise.resolve({
    ok: true,
    ack: {
      ok: true,
      message: 'start accepted',
      task_id: 'case-1',
      mission_loaded: true,
      mission_running: true,
      last_accepted_sequence: 9,
      vision_armed: true,
    },
  }));
  const request = store.startMission();
  expect(store.missionRunning).toBe(false);
  await request;
  expect(store.missionRunning).toBe(true);
});

it('ignores an incremental event at or below the snapshot sequence', () => {
  const store = useGroundStore();
  store.applySnapshot(snapshot({ snapshot_seq: 41, active_task_id: 'case-1', current_cell: 'A8B1' }));
  store.applyEvent(event({ seq: 40, task_id: 'case-1', event: 'telemetry',
                           payload: { current_cell: 'A1B1' } }));
  expect(store.currentCell).toBe('A8B1');
});

it('keeps authoritative state when a command fails', async () => {
  const store = useGroundStore();
  store.applySnapshot(snapshot({ mission_running: false }));
  mockPost('/api/mission/start', Promise.reject(new ApiError(504, 'command_timeout', '命令状态未知')));
  await expect(store.startMission()).rejects.toMatchObject({ errorCode: 'command_timeout' });
  expect(store.missionRunning).toBe(false);
});
```

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station/frontend && corepack pnpm install && corepack pnpm vitest run src/stores/ground.spec.ts`

Expected: FAIL because store/API/models do not exist.

- [ ] **Step 3: Implement typed models, HTTP functions, store, and reconnect flow**

Define `GroundSnapshot`, `WebEvent`, `AckSnapshot`, `CommandResponse` with the Gateway's exact
snake_case wire fields; dynamic payload is `Record<string, unknown>`. Pinia exposes camelCase
computed/action names to Vue components but performs the mapping only in `applySnapshot` and
`applyEvent`. `gateway.ts` must throw an `ApiError` carrying `status: number` and
`errorCode: string`, mapped from the response's snake_case `error_code`.
`useTelemetry()` uses same-origin `ws(s)://${location.host}/ws/telemetry`, exponential backoff
bounded at 5 seconds, and calls `fetchSnapshot()` after each successful reconnect before applying
later incremental messages.

`vite.config.ts` proxies `/api` and `/ws` to `http://127.0.0.1:8000`, with `ws: true` only for
`/ws`; no airborne or hotspot IP is compiled into frontend code.

Package scripts must be:

```json
{
  "dev": "vite",
  "build": "vue-tsc --noEmit && vite build",
  "typecheck": "vue-tsc --noEmit",
  "test": "vitest run"
}
```

Do not add `vite-plugin-checker`; `pnpm dev` must remain hot even with temporary type errors.

- [ ] **Step 4: Run GREEN and static checks**

Run:

```bash
cd web_ground_station/frontend
corepack pnpm test
corepack pnpm typecheck
corepack pnpm build
```

Expected: store tests PASS, typecheck PASS, production `dist/` builds.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/frontend
git commit -m "feat(web): establish the typed operator console"
```

### Task 8: Implement SVG H-Task Planning and Route Visualization

**Files:**
- Create: `web_ground_station/frontend/src/components/HMissionMap.vue`
- Create: `web_ground_station/frontend/src/components/HMissionMap.spec.ts`
- Create: `web_ground_station/frontend/src/panels/PlanningPanel.vue`
- Create: `web_ground_station/frontend/src/panels/PlanningPanel.spec.ts`
- Modify: `web_ground_station/frontend/src/App.vue`
- Modify: `web_ground_station/frontend/src/stores/ground.ts`

**Interfaces:**
- Consumes: canonical plan and parsed metadata returned by `/api/mission/plan`.
- Produces: `HMissionMap` props `plan`, `selectedNoFlyCells`, `editable`; emits `toggle-cell`.

- [ ] **Step 1: Write failing SVG and planning interaction tests**

```ts
it('renders 63 stable grid cells and distinct landing markers', () => {
  const wrapper = mount(HMissionMap, { props: { plan, selectedNoFlyCells: ['A2B2'], editable: true } });
  expect(wrapper.findAll('[data-cell]')).toHaveLength(63);
  expect(wrapper.get('[data-cell="A2B2"]').classes()).toContain('no-fly');
  expect(wrapper.get('[data-marker="descent-start"]').exists()).toBe(true);
  expect(wrapper.get('[data-marker="touchdown"]').exists()).toBe(true);
});

it('emits one cell toggle from click and keyboard activation', async () => {
  const wrapper = mount(HMissionMap, { props: { plan: null, selectedNoFlyCells: [], editable: true } });
  await wrapper.get('[data-cell="A2B2"]').trigger('click');
  expect(wrapper.emitted('toggle-cell')?.[0]).toEqual(['A2B2']);
});

it('offers explicit zoom and fit controls without changing the plan', async () => {
  const wrapper = mount(HMissionMap, { props: { plan, selectedNoFlyCells: [], editable: true } });
  const originalRoute = wrapper.get('[data-testid="route"]').attributes('points');
  const originalViewBox = wrapper.get('svg').attributes('viewBox');
  await wrapper.get('[aria-label="放大地图"]').trigger('click');
  expect(wrapper.get('svg').attributes('viewBox')).not.toBe(originalViewBox);
  await wrapper.get('[aria-label="适配地图"]').trigger('click');
  expect(wrapper.get('svg').attributes('viewBox')).toBe(originalViewBox);
  expect(wrapper.get('[data-testid="route"]').attributes('points')).toBe(originalRoute);
});
```

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station/frontend && corepack pnpm vitest run src/components/HMissionMap.spec.ts src/panels/PlanningPanel.spec.ts`

Expected: FAIL because map/panel are missing.

- [ ] **Step 3: Implement the viewBox-based SVG and planning workflow**

Render A1..A9 and B1..B7 using deterministic computed cell geometry. Use `<polyline>` for route,
separate `[data-marker]` elements for start, descent-start (`metadata.terminal_cell`) and touchdown
(`touchdown_x_cm/y_cm`). Every cell has `tabindex="0"`, `role="button"`, and at least a 44 px
effective target at the 1024 layout. Add icon buttons with accessible names for zoom in, zoom out,
and fit-to-map; they only change SVG `viewBox`. Planning panel enforces the existing three contiguous
no-fly cell workflow in UI, but treats Gateway/CLI rejection as authoritative.

- [ ] **Step 4: Run GREEN**

Run: `cd web_ground_station/frontend && corepack pnpm test && corepack pnpm typecheck`

Expected: map/planning tests and existing store tests PASS.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/frontend/src
git commit -m "feat(web): add H mission planning map"
```

### Task 9: Build the Responsive Runtime, Command, and Detection Console

**Files:**
- Create: `web_ground_station/frontend/src/panels/RuntimePanel.vue`
- Create: `web_ground_station/frontend/src/panels/DetectionPanel.vue`
- Create: `web_ground_station/frontend/src/components/CommandBar.vue`
- Create: `web_ground_station/frontend/src/components/StatusBar.vue`
- Create: `web_ground_station/frontend/src/panels/runtime.spec.ts`
- Create: `web_ground_station/frontend/src/styles/app.scss`
- Modify: `web_ground_station/frontend/src/App.vue`

**Interfaces:**
- Consumes: `groundStore` command gates and snapshot fields.
- Produces: complete single-page main operator workflow; no duplicate business state.

- [ ] **Step 1: Write failing command-gate and layout tests**

Test exact states:

```ts
import { createPinia, setActivePinia } from 'pinia';
import { mount } from '@vue/test-utils';
import { Quasar } from 'quasar';
import CommandBar from '../components/CommandBar.vue';
import { useGroundStore } from '../stores/ground';

it.each([
  [{ command: 'offline', loaded: true, running: false }, false, false],
  [{ command: 'online', loaded: true, running: false }, true, false],
  [{ command: 'online', loaded: true, running: true }, false, true],
])('gates START and STOP from server state', (state, canStart, canStop) => {
  const pinia = createPinia();
  setActivePinia(pinia);
  const store = useGroundStore();
  store.$patch({
    commandLink: state.command,
    missionLoaded: state.loaded,
    missionRunning: state.running,
  });
  const wrapper = mount(CommandBar, { global: { plugins: [pinia, Quasar] } });
  expect(wrapper.get('[data-testid="start-command"]').attributes('disabled') === undefined)
    .toBe(canStart);
  expect(wrapper.get('[data-testid="stop-command"]').attributes('disabled') === undefined)
    .toBe(canStop);
});
```

Mount `App` at 1024x600-equivalent container and assert status bar, SVG map and fixed command bar
exist simultaneously; verify details open in a Quasar dialog/drawer rather than resizing map.

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station/frontend && corepack pnpm vitest run src/panels/runtime.spec.ts`

Expected: FAIL because runtime components are missing.

- [ ] **Step 3: Implement dense three-breakpoint layout**

Use breakpoints `1440px` and `1024px`; do not scale font size with viewport. Standard/compact are
two-column, 1024x600 keeps top status and bottom command bars fixed, map in a constrained flexible
track, and moves logs/details into drawers. Buttons use Quasar loading/disabled states and at least
44x44 targets. START/STOP confirmation uses `QDialog`; HTTP response text is shown without
optimistically changing running state.

- [ ] **Step 4: Run GREEN and build**

Run: `cd web_ground_station/frontend && corepack pnpm test && corepack pnpm typecheck && corepack pnpm build`

Expected: all unit tests PASS and build succeeds.

- [ ] **Step 5: Commit**

```bash
git add web_ground_station/frontend/src
git commit -m "feat(web): add the responsive mission console"
```

### Task 10: Add Emergency STOP, Competition Startup, and Offline Preflight

**Files:**
- Create: `web_ground_station/gateway/nuedc_web_gateway/emergency_stop.py`
- Create: `web_ground_station/tests/gateway/test_emergency_stop.py`
- Create: `web_ground_station/scripts/start_dev.sh`
- Create: `web_ground_station/scripts/start_competition.sh`
- Create: `web_ground_station/scripts/check_web_ground_station.sh`
- Create: `web_ground_station/tests/test_scripts.sh`
- Create: `runtime/web_ground_station.env`
- Create: `runtime/web_ground_station.env.example`
- Modify: `.gitignore`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 reliable command builder; existing `scripts/check_ground_control_network.sh`.
- Produces: browser-independent `uv run python -m nuedc_web_gateway.emergency_stop --task-id ID`.
- Produces: standard competition entry `http://10.42.0.1:8000`.

- [ ] **Step 1: Write failing emergency and shell tests**

Emergency test uses fake transport and asserts exactly one STOP command with requested task_id.
Shell tests place fake `uv`, `corepack`, `pnpm`, network-check and planner executables on PATH, then
assert competition mode:

```text
loads runtime/web_ground_station.env
requires 10.42.0.1-compatible web host
runs network preflight
requires frontend/dist and planner CLI
does not invoke pnpm install, uv sync, --reload, or any PID Web receiver by default
executes uvicorn on 0.0.0.0:8000
```

- [ ] **Step 2: Run RED**

Run: `cd web_ground_station && uv run pytest tests/gateway/test_emergency_stop.py -v && bash tests/test_scripts.sh`

Expected: FAIL because CLI/scripts are missing.

- [ ] **Step 3: Implement emergency STOP and two startup modes**

`emergency_stop.py` loads config/proto, sends STOP through reliable transport, prints one concise
Chinese result, and exits `0` only on confirmed ACK. `start_dev.sh` runs proto generation, Gateway
with `--reload`, and Vite. `start_competition.sh` loads env, calls existing network check and new
preflight, verifies static `dist`, then `exec uv run uvicorn nuedc_web_gateway.app:create_app
--factory --host 0.0.0.0 --port 8000` without reload.

Commit `runtime/web_ground_station.env` with competition-safe non-secret defaults and keep the
example synchronized:

```dotenv
NUEDC_AIRBORNE_HOST=10.42.0.2
NUEDC_TELEMETRY_PORT=5557
NUEDC_COMMAND_PORT=5558
NUEDC_PID_DEBUG_ENABLED=0
NUEDC_PID_DEBUG_PORT=9870
NUEDC_WEB_HOST=0.0.0.0
NUEDC_WEB_PORT=8000
NUEDC_RUNTIME_DIR=runtime
NUEDC_PLANNER_CLI=build/shared/cpp/h_route_planner_cli
```

Add `!runtime/web_ground_station.env` after the existing `runtime/*.env` ignore rule so this one
non-secret, competition-standard configuration is tracked. `check_web_ground_station.sh` resolves
all relative paths from the repository root, verifies the configured host/ports and local files,
checks that `uv.lock` and `pnpm-lock.yaml` exist, and performs no package-manager or network access.

Register `test_web_ground_station_scripts` in top-level CTest and make scripts executable.

- [ ] **Step 4: Run GREEN**

Run:

```bash
cd web_ground_station
uv run pytest tests/gateway/test_emergency_stop.py -v
bash tests/test_scripts.sh
bash scripts/start_competition.sh --help
```

Expected: tests PASS; help does not start services.

- [ ] **Step 5: Commit**

```bash
git add .gitignore web_ground_station runtime/web_ground_station.env runtime/web_ground_station.env.example CMakeLists.txt
git commit -m "feat(web): add competition launch and emergency stop"
```

### Task 11: Close the Mock-Airborne, Responsive Browser, and Documentation Loop

**Files:**
- Create: `web_ground_station/frontend/playwright.config.ts`
- Create: `web_ground_station/tests/e2e/mission-flow.spec.ts`
- Create: `web_ground_station/tests/e2e/layout.spec.ts`
- Create: `web_ground_station/tests/e2e/fixtures.ts`
- Create: `web_ground_station/README.md`
- Modify: `README.md`
- Modify: `docs/dual_nuc_setup_guide.md`

**Interfaces:**
- Consumes: all earlier tasks plus existing `scripts/mock_airborne.py`.
- Produces: verified LOAD -> START -> telemetry/detection -> Summary browser workflow and handoff docs.

- [ ] **Step 1: Write failing Playwright mission test**

```ts
test('plans, loads, starts, observes and completes one H mission', async ({ page }) => {
  await page.goto('/');
  await page.getByTestId('cell-A2B2').click();
  await page.getByTestId('cell-A2B3').click();
  await page.getByTestId('cell-A2B4').click();
  await page.getByRole('button', { name: '生成航线' }).click();
  await expect(page.getByTestId('route')).toBeVisible();
  await page.getByRole('button', { name: '下发任务' }).click();
  await expect(page.getByText('任务已加载')).toBeVisible();
  await page.getByRole('button', { name: '执行任务' }).click();
  await expect(page.getByText(/巡查中/)).toBeVisible();
  await expect(page.getByTestId('detection-total')).not.toHaveText('0');
  await expect(page.getByText(/任务完成/)).toBeVisible({ timeout: 30_000 });
  await page.reload();
  await expect(page.getByText(/任务完成/)).toBeVisible();
  await expect(page.getByTestId('detection-total')).not.toHaveText('0');
});

test('resynchronizes authoritative state after Gateway restart', async ({ page, restartGateway }) => {
  await page.goto('/');
  await expect(page.getByTestId('command-link')).toHaveText(/在线/);
  await restartGateway();
  await expect(page.getByTestId('command-link')).toHaveText(/重新同步中|离线/);
  await expect(page.getByTestId('command-link')).toHaveText(/在线/, { timeout: 10_000 });
  await expect(page.getByTestId('start-command')).toBeEnabled();
});
```

Fixture starts mock airborne on reserved ports, Gateway with matching env, and production frontend;
it exposes a `restartGateway()` fixture that kills only Gateway, restarts it with the same runtime
directory, and waits for `/api/health`. Teardown terminates every process and records logs on failure.

- [ ] **Step 2: Write failing three-viewport layout test**

For `1920x1080`, `1366x768`, `1024x600`, assert:

```ts
await expect(page.getByTestId('status-bar')).toBeInViewport();
await expect(page.getByTestId('mission-map')).toBeInViewport();
await expect(page.getByTestId('command-bar')).toBeInViewport();
expect(await overlappingBoundingBoxes(page, ['status-bar', 'mission-map', 'command-bar'])).toEqual([]);
```

Collect browser requests during the test and assert every URL has origin equal to the local test
server; any CDN, analytics, font, update-check or other external origin fails the test.

- [ ] **Step 3: Run E2E RED**

Run: `cd web_ground_station/frontend && corepack pnpm playwright test`

Expected: FAIL until fixture/startup/data-testid details are complete.

- [ ] **Step 4: Complete fixtures, stable selectors, and operator docs**

Document exact dev, competition, PlotJuggler PID diagnostics, emergency STOP, offline preflight and worktree commands.
Update root README and dual-NUC guide to make `http://10.42.0.1:8000` the Web entry while explicitly
retaining Qt as migration fallback and prohibiting simultaneous control clients.

- [ ] **Step 5: Run focused Web verification**

Run:

```bash
cd web_ground_station
uv run pytest tests/gateway -v
uv run ruff check gateway tests/gateway
cd frontend
corepack pnpm test
corepack pnpm typecheck
corepack pnpm build
corepack pnpm playwright test
```

Expected: all Gateway, frontend and Playwright tests PASS at all three viewports.

- [ ] **Step 6: Run existing regression and shell gates**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: all existing 34 tests plus new CLI/shell tests PASS. The two ZeroMQ integration tests may
require local loopback socket permission; permission denial is not a product failure and must be
rerun with the required local-network capability.

- [ ] **Step 7: Perform the offline/hotspot acceptance run**

With external network disconnected, ground hotspot at `10.42.0.1`, airborne at `10.42.0.2`:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
uv sync --frozen --offline
cd frontend
corepack pnpm install --offline --frozen-lockfile
cd /home/sb/Ground_station/.worktrees/nuedc-web
source runtime/web_ground_station.env
web_ground_station/scripts/check_web_ground_station.sh
web_ground_station/scripts/start_competition.sh
```

Expected: both dependency checks use only prewarmed caches; Chromium loads `http://10.42.0.1:8000`;
planning, LOAD, START, telemetry, detection, STOP and Summary work; browser refresh and one Gateway
restart resynchronize from snapshot/ACK; PID diagnostics remain external to the Web UI and use the
existing UDP channel with PlotJuggler; no process attempts an Internet connection.

- [ ] **Step 8: Commit**

```bash
git add web_ground_station README.md docs/dual_nuc_setup_guide.md
git commit -m "test(web): close the operator mission loop"
```

## Final Review Gate

- [ ] Verify `git status --short` is clean.
- [ ] Verify `git log --oneline 9108190..HEAD` shows one reviewable commit per task.
- [ ] Compare implementation against every section of
  `docs/superpowers/specs/2026-07-20-web-ground-station-design.md`.
- [ ] Confirm no duplicate `.proto`, npm lockfile, Poetry lockfile, database, authentication layer,
  Router, PWA, Electron or unapproved Three.js/video implementation was introduced.
- [ ] Confirm Qt sources were not deleted and machine/airborne protocol compatibility was preserved.
