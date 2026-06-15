# Repository Guidelines

## 项目结构与模块组织
- `shared/proto/`：Protobuf 协议定义；修改 `messages.proto` 后需重新构建生成代码。
- `shared/cpp/`：C++ 共享核心库；`competition_core` 承载通用任务模型、TaskPlan 存储、Protobuf codec 与控制命令，`h_problem_core` 承载 H 题案例、规划、仿真和默认 Adapter。
- `airborne_computer/src/`：C++ 机载端源码；`airborne_computer/tests/`：Qt Test 单元测试。
- `ground_station_computer/src/`：Qt6 地面站源码；`ground_station_computer/tests/`：Qt Test 单元测试。
- `shared/cases/`：固定联调案例；`runtime/`：运行时任务计划；`build/`：本地构建产物，不应作为手工源码目录。

## 构建、测试与开发命令
- `cmake -S . -B build`：配置 CMake 工程并生成构建目录。
- `cmake --build build`：编译 C++ Protobuf、共享核心库、C++ 机载端与 Qt 地面站。
- `ctest --test-dir build --output-on-failure`：运行 C++/Qt 测试并输出失败详情。
- `./build/airborne_computer/airborne_app --case shared/cases/sample_case.json`：启动 C++ 机载端并加载样例案例。
- `./build/ground_station_computer/ground_station_app`：启动地面站。

## 编码风格与命名约定
- C++ 采用 C++17；文件名使用 `snake_case.cpp/.h`，类名使用 `PascalCase`，成员函数使用 `camelCase`。
- 保持小函数、显式错误信息和就近依赖导入；优先沿用现有 Qt Test 写法。
- 新增注释与文档请使用中文，代码标识符保持英文。

## 测试规范
- 共享核心、机载端和地面站测试均基于 `Qt6::Test`。
- 规划逻辑优先覆盖航线覆盖率、相邻性、代价与边界场景。
- 新增地面站测试文件放入 `ground_station_computer/tests/test_*.cpp`；新增机载端测试放入 `airborne_computer/tests/test_*.cpp`；共享核心测试放入 `shared/cpp/tests/test_*.cpp`。
- 修复缺陷时先补充最小失败用例，再修改实现；提交前至少运行受影响模块测试。
- 新增题目时优先新增 Adapter/Runtime，不直接把题目专有逻辑写入地面站 Shell 或 `competition_core`；详见 `docs/adding_task_adapter.md`。

## 提交与 Pull Request 规范
- 当前工作区未包含 `.git` 历史，无法提炼既有提交风格；建议使用祈使句、模块前缀格式，如：`airborne: add command server runtime`。
- PR 应说明变更目的、核心实现、测试结果与风险；涉及界面变更时附截图。
- 若修改协议、案例或规划逻辑，请在描述中注明影响范围与联调验证步骤。

## 配置与生成文件提示
- 不要直接编辑 `build/generated/proto/` 下的生成文件。
- 本仓库依赖 Qt6、Protobuf、ZeroMQ/`cppzmq`；环境缺失时优先修复依赖，再排查编译错误。
