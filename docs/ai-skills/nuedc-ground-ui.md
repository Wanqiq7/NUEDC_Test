# nuedc-ground-ui

## 触发场景

当任务涉及地面站界面、状态流、按钮可用性、演示效果或误操作防护时使用。

## 允许修改

- `web_ground_station/frontend/src/**`
- `web_ground_station/frontend/e2e/**`

## 工作规则

- 主界面只保留比赛现场需要的关键动作。
- 所有按钮状态必须由 Pinia ground store 和可靠 ACK 状态驱动。
- 调试信息默认收纳到次级区域，不占主操作路径。

## 禁止事项

- 不在 UI 里直接写协议细节。
- 不把规划逻辑塞进按钮回调。
- 不为了视觉效果破坏可读性和误操作防护。

## 必跑测试

```bash
cd web_ground_station/frontend
corepack pnpm test
corepack pnpm typecheck
corepack pnpm build
```

## 推荐提示词

请按比赛现场操作台标准优化地面站 UI，优先说明状态驱动、按钮约束、信息密度和测试覆盖。
