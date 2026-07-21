# Web-only Ground Station and Qt-free Planner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove every active Qt ground-station dependency while preserving the existing C++17 H-task planner as a stateless CLI with normalized-JSON behavior equivalent to the Qt baseline.

**Architecture:** Keep Vue, FastAPI, and the airborne wire path unchanged. Freeze the current Qt CLI output, replace only the planner's transitive closure with STL and `nlohmann_json`, then delete unrelated C++ protocol, simulator, state, and storage code. Verify compatibility at the binary boundary and through the unchanged Gateway worker-thread `subprocess.Popen` adapter.

**Tech Stack:** C++17, CMake 3.16+, STL, nlohmann_json, GoogleTest, Python 3.10/pytest, Vue 3/Vitest/Playwright, uv, pnpm.

## Global Constraints

- The sole native ground component is a Qt-free C++17 stateless route-planning CLI.
- Preserve `build/shared/cpp/h_route_planner_cli`, `NUEDC_PLANNER_CLI`, `h_planning_request_v1`, response fields, error codes, and exit codes `0/2/3/4`.
- Preserve search and waypoint order, repeats, XYZ/action/payload, route cost, optimality, A9-to-A1 and B1-to-B7 axes, descent-start versus touchdown, and `h_field_m_v1`.
- Do not modify `web_ground_station/gateway/nuedc_web_gateway/planner.py`; retain `asyncio.to_thread` plus synchronous `subprocess.Popen`.
- Keep Python Protobuf/pyzmq and `shared/proto/messages.proto`; remove only C++ Protobuf/ZeroMQ dependencies.
- Final active source/CMake contains no Qt6, QtTest, AUTOMOC, `Q_OBJECT`, or `#include <Q...>`.
- Do not change UI, planning behavior, wire protocol, PID/PlotJuggler scope, or airborne ROS 2 code.
- Work in the isolated Web worktree and preserve unrelated edits in `/home/sb/Ground_station/AGENTS.md`.
- Each implementation task receives spec-compliance review, then code-quality review.

## Final File Map

- Create `shared/cpp/include/h_problem_core/common/task_plan.h` for planner-only task plan values and JSON conversion.
- Retain and port `shared/cpp/{include,src}/h_problem_core/{common,mission,planning,tools}`.
- Retain `shared/cpp/tools/h_route_planner_cli_main.cpp` as stdin/stdout adapter only.
- Create `shared/cases/golden/planner_cli_cases.json` and capture/verify scripts under `shared/cpp/tests/`.
- Rewrite retained `shared/cpp/tests/test_h_*.cpp` with GoogleTest.
- Create `scripts/tests/test_web_only_ground_station.sh` for source/build boundaries.
- Delete `shared/cpp/include/competition_core`, H simulator, C++ protocol/storage/task sources, runtime fixture generator, and their tests.

---

### Task 1: Freeze the Qt CLI Baseline

**Files:**
- Create: `shared/cases/golden/planner_cli_cases.json`
- Create: `shared/cpp/tests/capture_planner_golden.py`
- Create: `shared/cpp/tests/verify_planner_golden.py`
- Modify: `shared/cpp/CMakeLists.txt`

**Interfaces:**
- Consumes: current Qt `build/shared/cpp/h_route_planner_cli`.
- Produces: fixture keys `name`, `stdin_text`, `exit_code`, `response`, `stderr_json_forbidden`; verifier CLI `verify_planner_golden.py PLANNER FIXTURE`.

- [ ] **Step 1: Write the failing verifier**

Run each case from repository root. Require exactly one stdout JSON document. Compare objects recursively, arrays in order, strings/bools exactly, numbers with `math.isclose(rel_tol=1e-12, abs_tol=1e-9)`, exit code exactly, and forbid `{` in stderr when requested.

- [ ] **Step 2: Verify the missing fixture fails**

Run: `python3 shared/cpp/tests/verify_planner_golden.py build/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json`

Expected: non-zero because the fixture is absent.

- [ ] **Step 3: Implement fixed baseline capture cases**

Capture in order: sample/default barrier; `A2B2/A2B3/A2B4`; raw `{`; wrong schema; empty case path; non-array no-fly; `A10B1`; missing case; blocked start `A9B1`. Preserve raw invalid stdin, compact object requests, refuse overwrite without `--overwrite`, and write stable indented JSON plus newline.

- [ ] **Step 4: Build old CLI, capture, and register CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target h_route_planner_cli --parallel
python3 shared/cpp/tests/capture_planner_golden.py --planner build/shared/cpp/h_route_planner_cli --output shared/cases/golden/planner_cli_cases.json
ctest --test-dir build -R 'test_h_route_planner_(cli|golden)' --output-on-failure
```

Expected: nine fixtures; old CLI and new binary verifier pass.

- [ ] **Step 5: Commit**

```bash
git add shared/cases/golden shared/cpp/tests/capture_planner_golden.py shared/cpp/tests/verify_planner_golden.py shared/cpp/CMakeLists.txt
git commit -m "test(planner): freeze Qt CLI golden behavior"
```

---

### Task 2: Migrate the Minimal Planner Closure

**Files:**
- Create: `shared/cpp/include/h_problem_core/common/task_plan.h`
- Modify: retained headers in `shared/cpp/include/h_problem_core/{common,mission,planning,tools}`
- Modify: retained sources in `shared/cpp/src/{mission,planning,tools}`
- Modify: `shared/cpp/tools/h_route_planner_cli_main.cpp`, `shared/cpp/CMakeLists.txt`
- Rewrite: `test_h_case_loader.cpp`, `test_h_mission_geometry.cpp`, `test_h_route_planner.cpp`, `test_h_planning_result.cpp`, `test_h_route_planner_cli.cpp`

**Interfaces:**
- Consumes: Task 1 golden fixture and current algorithm bodies.
- Produces: `runPlannerCliRequest(std::string_view)`, `loadCase(const std::filesystem::path&, std::string*)`, `buildTaskPlan(const CaseConfig&, std::optional<CellList>, std::string*)`, STL geometry/search APIs.

- [ ] **Step 1: Rewrite retained tests with GoogleTest**

Preserve every existing scenario, including exact small-grid optimality and all legal three-cell barriers. Add explicit assertions: `encodeCell(8,0) == "A9B1"`; `encodeCell(0,6) == "A1B7"`; A9B1 maps to mission `(0,0)`; repeats remain; terminal ID is `touchdown`; penultimate waypoint is descent-start cell; final waypoint is distinct `land` at `z=0`.

- [ ] **Step 2: Prove rewritten tests fail against Qt APIs**

Run: `cmake --build build --target test_h_case_loader test_h_mission_geometry test_h_route_planner test_h_planning_result test_h_route_planner_cli --parallel`

Expected: compile failure on STL interfaces or missing GoogleTest targets.

- [ ] **Step 3: Define exact planner values and containers**

Use `CellList = std::vector<std::string>`, `CellSet = std::set<std::string>`, `GridPoint { int x; int y; }`, fixed-width sequence integers, and planner-only `TaskWaypoint`/`TaskPlan`. Do not carry Ack, command state, events, summaries, mutexes, or Qt metatypes. Use ordered `std::set`/`std::map` wherever old iteration affects candidates or tie breaks; use unordered containers only for lookup-only data.

- [ ] **Step 4: Port geometry, cost, and route search mechanically**

Replace Qt collection/string methods without changing loop order, comparison tuples, search limits, or formulas. Replace Qt degree/radian helpers with a fixed C++17 Pi constant and local conversion functions. Preserve the landing candidate sampling order and epsilon values exactly.

- [ ] **Step 5: Port case and plan JSON**

Use `std::ifstream`, `std::filesystem::path`, and `nlohmann::json`. Validate JSON types before conversion and preserve existing English error classes. Preserve defaults `tick_interval_ms=100`, descent tolerance `5.0`, preferred heading `45.0`, heading tolerance `35.0`. Keep metadata and payload as compact JSON strings.

- [ ] **Step 6: Port the CLI contract**

Expose `PlannerCliResult { int exit_code; std::string stdout_bytes; std::string stderr_bytes; }`. Invalid document/object/schema/case path/no-fly array returns `2`; invalid cell, case load failure, or no route returns `3`; internal metadata failure returns `4`. Main reads all stdin, adds no stdout newline/log, writes diagnostics only to stderr, and returns the result code.

- [ ] **Step 7: Build and verify**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target h_route_planner_cli test_h_case_loader test_h_mission_geometry test_h_route_planner test_h_planning_result test_h_route_planner_cli --parallel
ctest --test-dir build -R 'test_h_(case|mission|route|planning)' --output-on-failure
python3 shared/cpp/tests/verify_planner_golden.py build/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json
ldd build/shared/cpp/h_route_planner_cli | rg 'Qt|protobuf|zmq'
```

Expected: tests/golden pass; final `rg` has no output and exit `1`.

- [ ] **Step 8: Commit**

```bash
git add shared/cpp/include/h_problem_core shared/cpp/src/mission shared/cpp/src/planning shared/cpp/src/tools shared/cpp/tools/h_route_planner_cli_main.cpp shared/cpp/tests/test_h_*.cpp shared/cpp/CMakeLists.txt
git commit -m "refactor(planner): remove Qt from route planning CLI"
```

---

### Task 3: Delete Qt-era Modules and Simplify CMake

**Files:**
- Delete: `shared/cpp/include/competition_core`, `shared/cpp/include/h_problem_core/runtime/simulator.h`
- Delete: `shared/cpp/src/{protocol,runtime,storage,task}`
- Delete: `shared/cpp/tools/generate_h_runtime_fixture.cpp`
- Delete: `test_h_simulator.cpp`, `test_json_codec.cpp`, `test_task_ports.cpp`, `test_task_protocol.cpp`
- Create: `scripts/tests/test_web_only_ground_station.sh`
- Modify: `CMakeLists.txt`, `shared/cpp/CMakeLists.txt`

**Interfaces:**
- Consumes: Qt-free planner from Task 2.
- Produces: build graph containing planner library/CLI/tests plus existing shell tests.

- [ ] **Step 1: Add and fail the source-boundary test**

Scan active top-level CMake, `shared/cpp`, `web_ground_station`, and `scripts` for `Qt6|QtTest|CMAKE_AUTOMOC|Q_OBJECT|#include[[:space:]]*<Q`; assert `ground_station_computer` and `shared/cpp/include/competition_core` are absent.

Run: `bash scripts/tests/test_web_only_ground_station.sh`

Expected: FAIL on current CMake/legacy files.

- [ ] **Step 2: Delete legacy files and dependencies**

Top-level CMake retains C++17, testing, and existing shell tests; adds Python3 Interpreter, nlohmann_json, GTest, and `add_subdirectory(shared/cpp)`. Remove AUTOMOC, Qt, C++ Protobuf generation, ZeroMQ/cppzmq, and `proto_messages`. C++ subdirectory exposes only planner library, CLI, five GoogleTests, and golden verifier.

- [ ] **Step 3: Clean-build without Qt**

```bash
cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release
cmake --build build-noqt --parallel
ctest --test-dir build-noqt --output-on-failure
bash scripts/tests/test_web_only_ground_station.sh
ldd build-noqt/shared/cpp/h_route_planner_cli | rg 'Qt|protobuf|zmq'
```

Expected: build/tests pass and linkage scan is empty.

- [ ] **Step 4: Commit**

```bash
git add -A CMakeLists.txt shared/cpp scripts/tests/test_web_only_ground_station.sh
git commit -m "build(ground): remove Qt-era native stack"
```

---

### Task 4: Test the Real CLI Through the Unchanged Gateway

**Files:**
- Modify: `web_ground_station/tests/gateway/test_planner.py`
- Do not modify: `web_ground_station/gateway/nuedc_web_gateway/planner.py`

**Interfaces:**
- Consumes: `NUEDC_TEST_PLANNER_CLI` and existing `PlannerClient`.
- Produces: real cross-language integration coverage.

- [ ] **Step 1: Add real-binary pytest**

Skip only when the variable is absent; if set, require the file. Plan sample/default barrier and assert A9B1, terminal `touchdown`, final `land`, `h_field_m_v1`, and `metadata.terminal_cell == waypoints[-2].id`.

- [ ] **Step 2: Verify optional and enabled modes**

```bash
cd web_ground_station
uv run pytest tests/gateway/test_planner.py -q -rs
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest tests/gateway/test_planner.py -q
git diff --exit-code HEAD -- gateway/nuedc_web_gateway/planner.py
```

Expected: first run has one explicit skip, second exercises real CLI, production adapter has no diff.

- [ ] **Step 3: Commit**

```bash
git add web_ground_station/tests/gateway/test_planner.py
git commit -m "test(web): exercise Qt-free planner integration"
```

---

### Task 5: Update Current Documentation and AGENTS.md

**Files:**
- Modify: `README.md`, `shared/cpp/README.md`, `AGENTS.md`, `scripts/tests/test_web_only_ground_station.sh`
- Modify carefully: `/home/sb/Ground_station/AGENTS.md`

**Interfaces:**
- Consumes: final structure and workspace ROS 2 guidance.
- Produces: Web-only build/test/ownership instructions without losing airborne guidance.

- [ ] **Step 1: Make boundary test reject obsolete current docs**

Scan only current README files and repository AGENTS for claims that the planner requires Qt, C++ Protobuf/ZeroMQ, simulator, command handler, or `competition_core`; exclude historical design records.

- [ ] **Step 2: Rewrite repository docs**

Document C++ packages `build-essential cmake nlohmann-json3-dev libgtest-dev`; remove old install/generation claims. State Web entry `http://10.42.0.1:8000`, planner path, `h_field_m_v1`, golden verification, real Gateway test, and PID via PlotJuggler.

- [ ] **Step 3: Update both AGENTS files**

Repository guidance uses STL/nlohmann_json/GoogleTest and forbids communication/state/UI in planner. Reread workspace AGENTS immediately before editing; replace only obsolete `NUEDC_Test` Qt paragraphs, preserve all ROS 2 and unrelated user text, and keep `shared/proto` as Python Gateway/airborne wire source.

- [ ] **Step 4: Verify and commit repository docs**

```bash
bash scripts/tests/test_web_only_ground_station.sh
git diff --check
git add README.md shared/cpp/README.md AGENTS.md scripts/tests/test_web_only_ground_station.sh
git commit -m "docs(ground): document Web-only planner stack"
```

Workspace `/home/sb/Ground_station/AGENTS.md` remains an explicit delivered edit outside this repository commit.

---

### Task 6: Full Regression and Deployment Checks

**Files:**
- Modify only if a test exposes an owning-component regression.
- Create/update: `.superpowers/sdd/progress.md`

**Interfaces:**
- Consumes: all migrated code/docs.
- Produces: exact native, Gateway, frontend, browser, boundary, and deployment evidence.

- [ ] **Step 1: Native and Gateway checks**

```bash
cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release
cmake --build build-noqt --parallel
ctest --test-dir build-noqt --output-on-failure
cd web_ground_station
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest -q
uv run ruff check gateway tests scripts
```

- [ ] **Step 2: Frontend and browser checks**

```bash
cd web_ground_station/frontend
pnpm test
pnpm typecheck
pnpm build
pnpm exec playwright test
```

Expected: all configured viewports including 1024x600 pass; no screenshot approval because UI is unchanged.

- [ ] **Step 3: Boundary/deployment checks and evidence**

```bash
cd ../..
bash scripts/tests/test_web_only_ground_station.sh
bash web_ground_station/tests/test_scripts.sh
bash web_ground_station/tests/test_cross_repo_manifest_contract.sh
ldd build-noqt/shared/cpp/h_route_planner_cli
```

Record commands/results and environment skips in progress. For a failure, add/tighten its test, make the smallest fix, rerun, and commit scoped. Never commit build trees, caches, runtime DB/session logs, screenshots, or frontend dist.

---

### Task 7: Independent Whole-branch Review and Handoff

**Files:**
- Review: `66ce1da..HEAD`
- Modify only for Critical/Important findings.

**Interfaces:**
- Consumes: all task commits/evidence.
- Produces: acceptance-clean branch; no merge/push without separate request.

- [ ] **Step 1: Dispatch independent review**

Review golden equivalence, deterministic tie breaks, exact CLI contract, no active Qt/C++ wire dependencies, preserved Python wire code, unchanged thread/Popen adapter, axes/repeats/descent/touchdown, retained ROS 2 AGENTS guidance, no generated artifacts, and direct plus Gateway integration coverage.

- [ ] **Step 2: Fix and re-review findings**

Expose each Critical/Important issue with a test, apply smallest correction, run focused test, commit, and re-review until none remain.

- [ ] **Step 3: Final verification and handoff**

```bash
git diff --check 66ce1da..HEAD
git status --short
ctest --test-dir build-noqt --output-on-failure
cd web_ground_station
NUEDC_TEST_PLANNER_CLI=../build-noqt/shared/cpp/h_route_planner_cli uv run pytest tests/gateway/test_planner.py -q
cd frontend
pnpm test && pnpm typecheck && pnpm build
```

Report commits, deletions, removed dependencies, exact test results, hardware-only residual validation, and workspace AGENTS changes. Do not merge or push unless requested.
