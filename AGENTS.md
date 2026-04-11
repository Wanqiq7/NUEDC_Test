# Repository Guidelines

## 项目结构与模块组织
- `shared/proto/`：Protobuf 协议定义；修改 `messages.proto` 后需重新构建生成代码。
- `airborne/uav_testbed/`：机载模拟端与航线规划核心逻辑；`airborne/uav_testbed/generated/` 为生成文件，勿手改。
- `airborne/tests/`：Python `unittest` 用例，文件命名遵循 `test_*.py`。
- `ground_control/src/`：Qt6 地面站源码；`ground_control/tests/`：Qt Test 单元测试。
- `shared/cases/`：固定联调案例；`runtime/`：运行时任务计划；`build/`：本地构建产物，不应作为手工源码目录。

## 构建、测试与开发命令
- `cmake -S . -B build`：配置 CMake 工程并生成构建目录。
- `cmake --build build`：编译 Qt 地面站、C++ Protobuf 与 Python Protobuf 代码。
- `ctest --test-dir build --output-on-failure`：运行 C++/Qt 测试并输出失败详情。
- `python3 -m unittest discover -s airborne/tests -v`：运行全部 Python 单元测试。
- `./build/ground_control/ground_control_app`：启动地面站。
- `PYTHONPATH=airborne python3 -m uav_testbed.run_simulator --case shared/cases/sample_case.json`：启动模拟端并加载样例案例。

## 编码风格与命名约定
- Python 使用 4 空格缩进、类型标注与 `snake_case`；类名使用 `PascalCase`。
- C++ 采用 C++17；文件名使用 `snake_case.cpp/.h`，类名使用 `PascalCase`，成员函数使用 `camelCase`。
- 保持小函数、显式错误信息和就近依赖导入；优先沿用现有 Qt/`unittest` 写法。
- 新增注释与文档请使用中文，代码标识符保持英文。

## 测试规范
- Python 测试基于 `unittest`，优先覆盖航线覆盖率、相邻性、代价与边界场景。
- Qt 测试基于 `Qt6::Test`，新增测试文件放入 `ground_control/tests/test_*.cpp`。
- 修复缺陷时先补充最小失败用例，再修改实现；提交前至少运行受影响模块测试。

## 提交与 Pull Request 规范
- 当前工作区未包含 `.git` 历史，无法提炼既有提交风格；建议使用祈使句、模块前缀格式，如：`python: optimize route planner tail search`。
- PR 应说明变更目的、核心实现、测试结果与风险；涉及界面变更时附截图。
- 若修改协议、案例或规划逻辑，请在描述中注明影响范围与联调验证步骤。

## 配置与生成文件提示
- 不要直接编辑 `build/generated/proto/` 或 `airborne/uav_testbed/generated/` 下的生成文件。
- 本仓库依赖 Qt6、Protobuf、ZeroMQ/`cppzmq`；环境缺失时优先修复依赖，再排查编译错误。
