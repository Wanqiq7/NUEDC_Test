#pragma once

#include <QString>

// 框架默认 task adapter id 的唯一事实源，消除此前散落在三处工厂函数里的
// "h_problem" 硬编码。题目无关，随框架长期存在。
QString defaultCompetitionTaskAdapterId();
