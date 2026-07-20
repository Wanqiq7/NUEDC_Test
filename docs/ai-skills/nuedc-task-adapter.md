# nuedc-task-adapter

## 触发场景

当用户要求新增电赛题目、题目页面或任务生成流程时使用。

## 允许修改

- `shared/cpp/include/<problem>_core/**`
- `shared/cpp/src/**`
- `web_ground_station/gateway/**`
- `web_ground_station/frontend/src/**`
- `web_ground_station/tests/**`

## 工作规则

- 新题目通过独立 C++ core、Gateway 转换和前端面板接入。
- 题目 UI、规则校验、规划状态和数据存储按现有 H 题边界组织。
- 题目规划输出必须转换成通用 `competition::TaskPlan`。

## 禁止事项

- 不复制 H 题通信主流程后只改文案。
- 不在 framework 通信层解析题目字段。
- 不直接编辑生成的 protobuf 文件。

## 必跑测试

```bash
ctest --test-dir build --output-on-failure
cd web_ground_station && uv run pytest tests/gateway -q
cd frontend && corepack pnpm test
```

## 推荐提示词

请为新题目设计共享 C++ core、Gateway 转换和前端面板，列出任务状态流、TaskPlan 转换、消息契约和测试用例。
