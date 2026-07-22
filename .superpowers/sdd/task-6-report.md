# Task 6 Report: Full Regression and Deployment Checks

Date: 2026-07-22

Baseline: `dac52ca08eef61dec90c57049ba078b41aac6b12`

Status: Complete. Native, real-planner Gateway, frontend browser, boundary, deployment,
and cross-repository manifest checks all passed. There is no remaining external-repository gap.

## Final verification evidence

| Command | Exact result |
| --- | --- |
| `cmake -S . -B build-final-fixes -DCMAKE_BUILD_TYPE=Release` | Fresh configure passed with GNU C++ 11.4.0, GTest 1.11.0, and nlohmann_json 3.10.5. |
| `cmake --build build-final-fixes --parallel` | Planner CLI and all five GoogleTest binaries built successfully. |
| `ctest --test-dir build-final-fixes --output-on-failure` | `100% tests passed, 0 tests failed out of 43`; discovered GoogleTest cases, shell tests, and golden verification all passed. |
| `python3 shared/cpp/tests/verify_planner_golden.py build-final-fixes/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json` | `verified 9 planner golden cases`. |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest tests/gateway/test_planner.py -k real_planner_cli_returns_execution_contract -v` | Real `PlannerClient` integration: `1 passed, 19 deselected in 0.12s`. |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest -q` | Real planner Gateway suite: `150 passed in 4.43s`. |
| `UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run ruff check gateway tests scripts` | `All checks passed!`. |
| `corepack pnpm test` | Vitest: 8 files and 53 tests passed. |
| `corepack pnpm typecheck` | `vue-tsc --noEmit` passed. |
| `corepack pnpm build` | Typecheck and Vite production build passed. |
| `corepack pnpm exec playwright test` | `4 passed (12.5s)`, including the configured 1024x600 viewport. |
| `bash scripts/tests/test_web_only_ground_station.sh` | Source boundary clean; current documentation describes the Web-only boundary. |
| `bash web_ground_station/tests/test_scripts.sh` | `web ground station scripts: PASS`; `MediaMTX supervisor: PASS`. |
| `bash web_ground_station/tests/test_cross_repo_manifest_contract.sh /home/sb/Ground_station/.worktrees/airborne-stack-competition` | `cross-repo deployment manifest contract: PASS (/tmp/tmp.JWgzP8p23N/ground/runtime/deployment_manifest.json)`. |
| `ldd build-final-fixes/shared/cpp/h_route_planner_cli` | Only libc, libstdc++, libm, libgcc_s, loader, and linux-vdso; no Qt, Protobuf, or ZeroMQ linkage. |

The full Gateway and Playwright commands require loopback sockets. Their sandboxed attempts
failed with `EPERM`; approved outside-sandbox reruns above passed without source changes.
Generated build, frontend dist, browser output, caches, and runtime artifacts remain ignored.
