# Repository Guidelines

## 项目结构与模块组织
- `web_ground_station/gateway/`：FastAPI Gateway、WebSocket 消息转换和机载通信。
- `web_ground_station/frontend/`：Vue 3、Quasar 和 Pinia 单页主控台。
- `shared/proto/`：Protobuf 协议定义；修改 `messages.proto` 后需重新构建生成代码。
- `shared/cpp/`：C++ 共享核心库与无状态 H 题规划 CLI。
- `shared/cases/`：固定联调案例；`runtime/`：运行时任务计划；`build/`：生成目录。

## 构建、测试与开发命令
- `cmake -S . -B build && cmake --build build`：构建共享 C++ 核心和规划 CLI。
- `ctest --test-dir build --output-on-failure`：运行共享 C++ 测试。
- `cd web_ground_station && uv run pytest tests/gateway -q`：运行 Gateway 测试。
- `cd web_ground_station/frontend && corepack pnpm test`：运行前端单元测试。
- `web_ground_station/scripts/start_dev.sh`：启动开发模式；比赛使用 `start_competition.sh`。

## 编码风格与命名约定
- C++ 采用 C++17；文件名使用 `snake_case.cpp/.h`，类名使用 `PascalCase`，成员函数使用 `camelCase`。
- Python 遵循现有 Ruff 规则；Vue/TypeScript 沿用 Composition API 和现有组件模式。
- 新增注释与文档请使用中文，代码标识符保持英文。

## 测试规范
- 共享 C++ 测试放入 `shared/cpp/tests/test_*.cpp`。
- Gateway 测试放入 `web_ground_station/tests/gateway/`，前端测试与组件就近放置。
- 规划逻辑优先覆盖航线覆盖率、相邻性、代价与边界场景。
- 修复缺陷时先补充最小失败用例；UI 变更至少运行 Vitest、类型检查和生产构建。
- 新题目在 Gateway 消息转换和前端面板中扩展，不把题目专有逻辑写入 `competition_core`。

## 提交与 Pull Request 规范
- 使用简洁的祈使句和范围前缀，例如 `feat(web): add mission replay`。
- PR 应说明行为变化、测试结果、部署影响；涉及界面变更时附截图。
- 若修改协议、案例或规划逻辑，请明确兼容性影响与联调步骤。

## 配置与生成文件提示
- 不要编辑或提交 `build/generated/proto/`、`web_ground_station/runtime/` 等生成内容。
- C++ 规划核心仍依赖 Qt6 Core/Test、Protobuf 和 ZeroMQ；Web 运行环境依赖 Python、uv、Node.js 和 pnpm。
