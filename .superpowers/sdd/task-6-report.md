# 任务 6 报告：完整回归与部署检查

日期：2026-07-22

基线：`dac52ca08eef61dec90c57049ba078b41aac6b12`

状态：完成。原生、真实规划器 Gateway、前端浏览器、边界、部署和跨仓库清单检查均已通过，
不存在遗留的外部仓库缺口。

## 最终验证证据

| 命令 | 精确结果 |
| --- | --- |
| `cmake -S . -B build-final-fixes -DCMAKE_BUILD_TYPE=Release` | 使用 GNU C++ 11.4.0、GTest 1.11.0 和 nlohmann_json 3.10.5 的全新配置通过。 |
| `cmake --build build-final-fixes --parallel` | 规划器 CLI 和全部五个 GoogleTest 二进制构建成功。 |
| `ctest --test-dir build-final-fixes --output-on-failure` | `100% tests passed, 0 tests failed out of 43`；自动发现的 GoogleTest 案例、shell 测试和 golden 验证均通过。 |
| `python3 shared/cpp/tests/verify_planner_golden.py build-final-fixes/shared/cpp/h_route_planner_cli shared/cases/golden/planner_cli_cases.json` | `verified 9 planner golden cases`。 |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest tests/gateway/test_planner.py -k real_planner_cli_returns_execution_contract -v` | 真实 `PlannerClient` 集成：`1 passed, 19 deselected in 0.12s`。 |
| `NUEDC_TEST_PLANNER_CLI=../build-final-fixes/shared/cpp/h_route_planner_cli UV_CACHE_DIR=/tmp/nuedc-final-fixes-uv uv run pytest -q` | 真实规划器 Gateway 测试套件：`150 passed in 4.43s`。 |
| `UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run ruff check gateway tests scripts` | `All checks passed!`。 |
| `corepack pnpm test` | Vitest：8 个文件、53 个测试通过。 |
| `corepack pnpm typecheck` | `vue-tsc --noEmit` 通过。 |
| `corepack pnpm build` | 类型检查和 Vite 生产构建通过。 |
| `corepack pnpm exec playwright test` | `4 passed (12.5s)`，包含已配置的 1024x600 视口。 |
| `bash scripts/tests/test_web_only_ground_station.sh` | 源码边界检查通过；当前文档正确描述 Web-only 边界。 |
| `bash web_ground_station/tests/test_scripts.sh` | `web ground station scripts: PASS`；`MediaMTX supervisor: PASS`。 |
| `bash web_ground_station/tests/test_cross_repo_manifest_contract.sh /home/sb/Ground_station/.worktrees/airborne-stack-competition` | `cross-repo deployment manifest contract: PASS (/tmp/tmp.JWgzP8p23N/ground/runtime/deployment_manifest.json)`。 |
| `ldd build-final-fixes/shared/cpp/h_route_planner_cli` | 仅链接 libc、libstdc++、libm、libgcc_s、loader 和 linux-vdso；未链接 Qt、Protobuf 或 ZeroMQ。 |

完整 Gateway 和 Playwright 命令需要回环套接字。沙箱内尝试因 `EPERM` 失败；上述获批的
沙箱外重跑在未修改源码的情况下通过。生成的构建目录、前端 dist、浏览器输出、缓存和
运行时产物均保持忽略。
