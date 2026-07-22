# 最终修复报告

日期：2026-07-22

基线：`dac52ca08eef61dec90c57049ba078b41aac6b12`

## 已解决的问题

1. 恢复原 Qt case loader 的行为：非数组 `animals` 值会被忽略，非对象
   `landing` 值也会被忽略。`animals` 数组仍会拒绝非对象元素、空对象 `{}`、
   缺少 `cell` 的对象和缺少 `name` 的对象；`landing` 对象仍会拒绝缺少必填值的情况。
2. 保持忽略非对象 `landing` 值时的 CLI 失败分类：退出码为 3，
   `error_code=planning_failed`，而不是 `case_load_failed`。
3. 通过 `gtest_discover_tests` 注册全部五个 GoogleTest 二进制，使用仓库根目录作为
   `WORKING_DIRECTORY`，并设置 `DISCOVERY_MODE PRE_TEST`。保留 golden 测试和 shell 测试。
4. 补齐任务 6 中过时的跨仓库验证缺口，并根据当前证据将任务 7 标记为完成。

未修改 Gateway 生产代码、UI、协议、case golden、运行时计划或规划算法。

## RED/GREEN 证据

| 阶段 | 命令 | 结果 |
| --- | --- | --- |
| RED | `cmake --build build-noqt --target test_h_case_loader test_h_route_planner_cli --parallel && ctest --test-dir build-noqt -R 'test_h_(case_loader|route_planner_cli)$' --output-on-failure` | Loader：新增 2 个失败；CLI：新增 1 个失败，覆盖全部三种非对象 `landing` 值。这些失败符合严格 loader 回归的预期。 |
| GREEN | `cmake -S . -B build-noqt -DCMAKE_BUILD_TYPE=Release && cmake --build build-noqt --target test_h_case_loader test_h_route_planner_cli --parallel && build-noqt/shared/cpp/test_h_case_loader && build-noqt/shared/cpp/test_h_route_planner_cli` | Loader 14/14、CLI 7/7 通过。 |

## 最终验证

| 命令 | 结果 |
| --- | --- |
| `cmake -S . -B build-final-fixes -DCMAKE_BUILD_TYPE=Release` | 全新配置通过。 |
| `cmake --build build-final-fixes --parallel` | 完整原生构建通过。 |
| `ctest --test-dir build-final-fixes --output-on-failure` | 8.32 秒内 `100% tests passed, 0 tests failed out of 43`。首次提交前运行结果为 42/43，因为部署测试会按设计拒绝已跟踪的脏改动；提交后重跑通过。 |
| `python3 shared/cpp/tests/verify_planner_golden.py build-final-fixes/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json` | `verified 9 planner golden cases`。 |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest tests/gateway/test_planner.py -k real_planner_cli_returns_execution_contract -v` | `1 passed, 19 deselected in 0.12s`。 |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest -q` | `150 passed in 4.43s`。 |
| `corepack pnpm exec playwright test` | `4 passed (12.5s)`。 |
| `bash scripts/tests/test_web_only_ground_station.sh` | 源码与文档边界检查通过。 |
| `bash web_ground_station/tests/test_cross_repo_manifest_contract.sh /home/sb/Ground_station/.worktrees/airborne-stack-competition` | `cross-repo deployment manifest contract: PASS (/tmp/tmp.JWgzP8p23N/ground/runtime/deployment_manifest.json)`。 |
| `ldd build-final-fixes/shared/cpp/h_route_planner_cli` | 不依赖 Qt、Protobuf 或 ZeroMQ。 |
| `git diff --check` | 在范围内代码提交前和报告提交前均通过。 |

## 改动文件

- `shared/cpp/src/mission/case_loader.cpp`
- `shared/cpp/CMakeLists.txt`
- `shared/cpp/tests/test_h_case_loader.cpp`
- `shared/cpp/tests/test_h_route_planner_cli.cpp`
- `.superpowers/sdd/task-6-report.md`
- `.superpowers/sdd/progress.md`
- `.superpowers/sdd/final-fixes-report.md`

## 自审与关注点

实现仅限两个兼容性分支和测试注册。测试覆盖 `null`、字符串、对象和数组的容器边界，
包括保留的严格内部校验；动物数组测试明确覆盖非对象、空对象 `{}`、缺少 `cell` 和缺少
`name`。CLI 回归通过真实 case 文件 loader 和规划器验证失败分类。

没有已知功能问题。完整 Gateway 和 Playwright 运行需要回环套接字权限；沙箱内尝试因
`EPERM` 失败，随后获批的沙箱外重跑在未修改源码的情况下通过。临时构建、缓存、dist
和浏览器产物均已忽略。
