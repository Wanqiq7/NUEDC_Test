#pragma once

#include "competition_core/task/models.h"

#include <QJsonObject>

#include <optional>

namespace competition {

QJsonObject taskPlanToJson(const TaskPlan &plan);
std::optional<TaskPlan> taskPlanFromJsonObject(const QJsonObject &object, QString *error_message = nullptr);
bool validateTaskPlan(const TaskPlan &plan, QString *error_message = nullptr);
bool storeTaskPlan(const TaskPlan &plan, const QString &output_path, QString *error_message = nullptr);
std::optional<TaskPlan> loadTaskPlan(const QString &path, QString *error_message = nullptr);

} // namespace competition
