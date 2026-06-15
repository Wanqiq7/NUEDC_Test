#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

constexpr int MapWidth = 9;
constexpr int MapHeight = 7;

std::optional<MissionPlan> buildMissionPlan(
    const CaseConfig &case_config,
    std::optional<QStringList> override_no_fly_cells = std::nullopt,
    QString *error_message = nullptr);

} // namespace hcore
