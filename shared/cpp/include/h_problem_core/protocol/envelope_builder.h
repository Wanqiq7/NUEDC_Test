#pragma once

#include "competition_core/protocol/envelope_codec.h"
#include "competition_core/task/models.h"
#include "h_problem_core/common/models.h"

#include "messages.pb.h"

namespace hcore {

using competition::buildAckBytes;
using competition::buildAckEnvelope;
using competition::buildMissionLoadEnvelope;
using competition::buildTaskEventEnvelope;
using competition::buildTaskPlanEnvelope;
using competition::buildTaskSummaryEnvelope;
using competition::taskEventToMessage;
using competition::taskPlanFromMessage;
using competition::taskPlanToMessage;
using competition::taskSummaryToMessage;

competition::TaskPlan taskPlanFromMissionPlan(const MissionPlan &plan);
std::optional<MissionPlan> missionPlanFromTaskPlan(const competition::TaskPlan &plan, QString *error_message = nullptr);

Envelope buildGridConfigEnvelope(quint64 sequence, const MissionPlan &plan);
Envelope buildMissionLoadEnvelope(const MissionPlan &plan);
Envelope buildTelemetryEnvelope(quint64 sequence, const QString &cell, quint32 step_index, quint32 visited_cells);
Envelope buildDetectionEnvelope(quint64 sequence, const QString &cell, const QString &animal_name, quint32 count);
Envelope buildSummaryEnvelope(quint64 sequence, const QMap<QString, quint32> &totals, quint32 visited_cells);
std::optional<MissionPlan> missionPlanFromGridConfig(const TaskPlanMessage &config, QString *error_message = nullptr);

} // namespace hcore
