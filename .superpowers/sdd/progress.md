Task 1: complete (commits 970fd3c..e1a4194, review clean; Qt/Web sender exclusivity deferred to startup/integration tasks)
Task 2: complete (commits e1a4194..19cd367, approved; minors: assert all competition defaults, consider protoc/runtime version guard)
Task 3: complete (commits 19cd367..54bdb47, review clean; user-approved sync Popen in worker thread, minor: 5ms compatibility polling)
Task 4: complete (commits 54bdb47..e5ae408, review clean; separate ACK/event sequence domains and atomic recorder shutdown)
Task 5: complete (commits e5ae408..ede3545, approved; minors deferred: whitespace assertion, Task6 active-socket context shutdown)
Task 6: complete (commits ede3545..2aaaf13, approved; minors: path/cancel/canonical negative test coverage for final review)
Task 7: complete (commits 2aaaf13..e0ddc1c, review clean; 17 tests, strict typecheck/build, local locked assets)
Task 8: complete (commits e0ddc1c..cd43581, review clean; 26 tests and Chromium 1024x600 marker/map verification)
Task 9: complete (commits c37450d..4b8571e, review clean; 38 tests, typecheck/build/diff checks; real-browser viewport verification remains sandbox-blocked)
Task 10 (Web PID debug workspace): removed by user decision; existing UDP PID diagnostics remain external via PlotJuggler, with no Web receiver or page.
Task 10 (emergency/startup): complete (commits aca6217..02aeac5, review approved; focused app/config/emergency tests 34 passed, shell gates passed; full gateway socket tests remain sandbox-blocked)
Task 11: complete (commits d78c3c4..da71cb6, review approved; frontend 41, typecheck/build, Ruff, shell gates, CMake build and non-socket CTest passed; browser/socket E2E remain sandbox-blocked)
Whole-branch review: approved after commits 2056556 and efab812; Gateway 109 and frontend 46 tests pass. Remaining acceptance gap is browser/Vite local-listen EPERM plus physical hotspot run.

## 2026-07-21 Ground/Airborne Stack Optimization

Plan: docs/superpowers/plans/2026-07-21-ground-airborne-stack-optimization.md
Ground base: 52fdfdb3e42bc30fd55f827d634fec53b910e3b2
Airborne base: 24ec03e04ec572c83a1a9a55a1209125719c4edd
Task 1: complete (commits 52fdfdb..994d53c, review clean; 48 tests, typecheck and production build passed)
Task 2: complete (airborne commits 24ec03e..f2961c8, review clean; RED/GREEN and registered config expectations aligned; package-level ROS verification blocked by missing RKNN SDK/install artifacts)
Task 3: complete (commits 994d53c..a41cdf5, review clean; Gateway 113 tests, Ruff and shell gates passed; generated artifacts remain ignored)
Task 4: complete (ground commits a41cdf5..5f6b7c3; airborne commits f2961c8..71a91d2; final review clean; Ground 12 and Airborne 11 focused tests; colcon environment gap retained)
Task 5: complete (airborne commits 71a91d2..5b0e0bf, review clean; sim bringup isolated and both packages discovered; full colcon build blocked by RKNN/install environment)
Task 6: complete (airborne commits 5b0e0bf..43c1d44, code review approved; 19 focused tests/shell/CTest/install passed; disabled-propeller dual-NUC five-point acceptance pending)
Task 7: complete (airborne commits 43c1d44..4033ac3, code review approved; 7 strict metrics/cadence tests and cam_node syntax passed; RKNN SDK build and 10-minute hardware p99 gate pending)
Final whole-branch review: approved after Ground b38942e/6969951 and Airborne fba601c/19300c0/fcde5c5/7e159e1 fixes; no Critical or Important code findings remain.
Competition branch: feat/stack-optimization-airborne-competition at 725170c, based on 43c1d44 with only Phase A/B fixes a0484a7/3c05a11/5196a14/725170c; no metrics files.
Post-competition branch: feat/stack-optimization-airborne at 7e159e1 with Phase C metrics db75198; strict and ASan/UBSan metrics tests 10/10.
Final verification: Ground frontend 48 tests + typecheck/build, Gateway focused 18 + Ruff/shell, real cross-repo contract PASS; Competition deployment tests 62 passed/1 root-only skip and flight_control_adapter 16/16.
Acceptance gates pending: real root /opt install as User=sb, systemd/dual-NUC flow, disabled-propeller STM32/motor zero response, physical hotspot, complete ROS/RKNN resources, and 10-minute RK3588/Point-LIO benchmark.

## 2026-07-22 Web-only 无 Qt 规划器迁移

计划：docs/superpowers/plans/2026-07-22-web-only-qt-free-planner.md
基线：3109ccf36af7485a04ab025192f77f2fd22669d8
任务 1：完成（提交 3109ccf..b7cf3fd，审查无问题；9 个 golden 案例，CTest 2/2）
任务 2：完成（提交 b7cf3fd..1b8166f，修复解析器、范围和溢出问题后审查无问题；CTest 6/6，golden 9/9，无 Qt wire 链接）
任务 3：完成（提交 1b8166f..b595da6，已批准；全新 CTest 9/9，边界和 ldd 检查通过；次要项：单引号文本 include 与 helper 错误路径自动化留待最终审查）
任务 4：完成（提交 b595da6..c2bdf18，审查无问题；可选模式 19 通过/1 跳过，真实 CLI 20 通过，生产适配器未改动）
任务 5：完成（提交 c2bdf18..dac52ca，修正文档边界措辞后审查无问题；已更新文档和工作区 AGENTS）
任务 6：完成（全新原生 CTest 43/43，含自动发现的 GoogleTest；真实规划器 Gateway 150 通过；Ruff 通过；前端 53/53 加 typecheck/build；Playwright 4/4，含 1024x600；边界、部署及跨仓库清单契约均通过；无外部缺口）
任务 7：完成（全分支问题已修复；通过 RED/GREEN loader 与 CLI 回归恢复 Qt case 字段兼容性；五个 GoogleTest 二进制使用自动发现；最终验证与交接报告完成）
