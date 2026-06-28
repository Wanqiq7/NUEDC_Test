#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

struct SimulatedTaskStream {
    competition::TaskPlan plan;
    QVector<competition::TaskEvent> events;
    competition::TaskSummary summary;
};

std::optional<SimulatedTaskStream> simulateTaskStream(
    const CaseConfig &case_config,
    std::optional<MissionPlan> mission_plan = std::nullopt,
    QString *error_message = nullptr);

QVector<SimMessage> simulateMessages(
    const CaseConfig &case_config,
    std::optional<MissionPlan> mission_plan = std::nullopt,
    QString *error_message = nullptr);

} // namespace hcore
