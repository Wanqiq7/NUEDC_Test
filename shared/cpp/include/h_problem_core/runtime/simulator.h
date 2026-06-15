#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

QVector<SimMessage> simulateMessages(
    const CaseConfig &case_config,
    std::optional<MissionPlan> mission_plan = std::nullopt,
    QString *error_message = nullptr);

} // namespace hcore
