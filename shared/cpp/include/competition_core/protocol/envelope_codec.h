#pragma once

#include "competition_core/task/models.h"

#include "messages.pb.h"

#include <QByteArray>

#include <optional>

namespace competition {

Envelope buildTaskPlanEnvelope(quint64 sequence, const TaskPlan &plan);
Envelope buildMissionLoadEnvelope(const TaskPlan &plan);
Envelope buildTaskEventEnvelope(quint64 sequence, const TaskEvent &event);
Envelope buildTaskSummaryEnvelope(quint64 sequence, const TaskSummary &summary);
Envelope buildAckEnvelope(bool success, const QString &message);
QByteArray buildAckBytes(bool success, const QString &message);

TaskPlanMessage taskPlanToMessage(const TaskPlan &plan);
TaskEventMessage taskEventToMessage(const TaskEvent &event);
TaskSummaryMessage taskSummaryToMessage(const TaskSummary &summary);
std::optional<TaskPlan> taskPlanFromMessage(const TaskPlanMessage &message, QString *error_message = nullptr);

} // namespace competition
