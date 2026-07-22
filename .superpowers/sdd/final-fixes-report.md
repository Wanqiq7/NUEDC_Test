# Final Fixes Report

Date: 2026-07-22

Baseline: `dac52ca08eef61dec90c57049ba078b41aac6b12`

## Findings resolved

1. Restored original Qt case-loader behavior: `animals` values that are not arrays are
   ignored, and `landing` values that are not objects are ignored. Arrays still reject
   non-object animal entries, and landing objects still reject missing required values.
2. Preserved CLI failure classification for ignored non-object landing values: exit 3 with
   `error_code=planning_failed`, rather than `case_load_failed`.
3. Registered all five GoogleTest binaries through `gtest_discover_tests`, using the repository
   root as `WORKING_DIRECTORY` and `DISCOVERY_MODE PRE_TEST`. Golden and shell tests remain.
4. Closed the stale Task 6 cross-repository gap and marked Task 7 complete with current evidence.

No Gateway production code, UI, protocol, case golden, runtime plan, or planning algorithm was changed.

## RED/GREEN evidence

| Phase | Command | Result |
| --- | --- | --- |
| RED | `cmake --build build-noqt --target test_h_case_loader test_h_route_planner_cli --parallel && ctest --test-dir build-noqt -R 'test_h_(case_loader|route_planner_cli)$' --output-on-failure` | Loader: 2 new failures; CLI: 1 new failure covering all three non-object landing values. Failures were the expected strict-loader regression. |
| GREEN | `cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release && cmake --build build-noqt --target test_h_case_loader test_h_route_planner_cli --parallel && build-noqt/shared/cpp/test_h_case_loader && build-noqt/shared/cpp/test_h_route_planner_cli` | Loader 14/14 and CLI 7/7 passed. |

## Final verification

| Command | Result |
| --- | --- |
| `cmake -S . -B build-final-fixes -DCMAKE_BUILD_TYPE=Release` | Fresh configure passed. |
| `cmake --build build-final-fixes --parallel` | Full native build passed. |
| `ctest --test-dir build-final-fixes --output-on-failure` | `100% tests passed, 0 tests failed out of 43` in 8.32 s. The first pre-commit run was 42/43 because the deployment test intentionally rejects tracked dirty changes; the post-commit rerun passed. |
| `python3 shared/cpp/tests/verify_planner_golden.py build-final-fixes/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json` | `verified 9 planner golden cases`. |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest tests/gateway/test_planner.py -k real_planner_cli_returns_execution_contract -v` | `1 passed, 19 deselected in 0.12s`. |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest -q` | `150 passed in 4.43s`. |
| `corepack pnpm exec playwright test` | `4 passed (12.5s)`. |
| `bash scripts/tests/test_web_only_ground_station.sh` | Source and documentation boundary checks passed. |
| `bash web_ground_station/tests/test_cross_repo_manifest_contract.sh /home/sb/Ground_station/.worktrees/airborne-stack-competition` | `cross-repo deployment manifest contract: PASS (/tmp/tmp.JWgzP8p23N/ground/runtime/deployment_manifest.json)`. |
| `ldd build-final-fixes/shared/cpp/h_route_planner_cli` | No Qt, Protobuf, or ZeroMQ dependency. |
| `git diff --check` | Passed before the scoped code commit and before the report commit. |

## Changed files

- `shared/cpp/src/mission/case_loader.cpp`
- `shared/cpp/CMakeLists.txt`
- `shared/cpp/tests/test_h_case_loader.cpp`
- `shared/cpp/tests/test_h_route_planner_cli.cpp`
- `.superpowers/sdd/task-6-report.md`
- `.superpowers/sdd/progress.md`
- `.superpowers/sdd/final-fixes-report.md`

## Self-review and concerns

The implementation is limited to the two compatibility branches and test registration. Tests
cover null, string, object, and array container boundaries, including the retained strict inner
validation. The CLI regression exercises the real case file loader and planner classification.

There are no known functional concerns. Full Gateway and Playwright runs need loopback socket
permission; their sandboxed attempts failed with `EPERM`, then approved outside-sandbox reruns
passed without source changes. Temporary build, cache, dist, and browser artifacts are ignored.
