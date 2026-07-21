#pragma once

#include "h_problem_core/common/models.h"
#include "h_problem_core/common/task_plan.h"

namespace hcore {

constexpr int MapWidth = 9;
constexpr int MapHeight = 7;
inline constexpr char HExecutionContract[] = "h_field_m_v1";
inline constexpr char HTouchdownWaypointId[] = "touchdown";

std::optional<TaskPlan> buildTaskPlan(
    const CaseConfig &case_config,
    std::optional<CellList> override_no_fly_cells = std::nullopt,
    std::string *error_message = nullptr);

} // namespace hcore
