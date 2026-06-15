#pragma once

#include "competition_core/mission/task_plan_store.h"
#include "competition_core/task/models.h"
#include "h_problem_core/common/models.h"

#include <QJsonObject>

namespace hcore {

using competition::loadTaskPlan;
using competition::storeTaskPlan;
using competition::taskPlanFromJsonObject;
using competition::taskPlanToJson;
using competition::validateTaskPlan;

QJsonObject missionPlanToJson(const MissionPlan &plan);
std::optional<MissionPlan> missionPlanFromJsonObject(const QJsonObject &object, QString *error_message = nullptr);
bool validateMissionPlan(const MissionPlan &plan, QString *error_message = nullptr);
bool storeMissionPlan(const MissionPlan &plan, const QString &output_path, QString *error_message = nullptr);
std::optional<MissionPlan> loadMissionPlan(const QString &path, QString *error_message = nullptr);

} // namespace hcore
