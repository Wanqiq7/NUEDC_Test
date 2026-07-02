#pragma once

#include "framework/task/competition_task_adapter.h"

// H 题向框架注册表暴露的唯一入口：只提供 descriptor（id / 显示名 / 工厂），
// 不泄漏任何 H 题 UI / 规则 / 存储类型。框架注册表 include 本头以装配 H 题，
// 这是框架 -> 题目方向唯一被允许的依赖点（等价于依赖注入根）。
CompetitionTaskAdapterDescriptor hProblemTaskAdapterDescriptor();
