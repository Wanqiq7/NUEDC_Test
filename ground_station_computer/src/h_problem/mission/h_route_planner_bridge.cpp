#include "h_problem/mission/h_route_planner_bridge.h"

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"
competition::TaskPlanningResult HRoutePlanner::generatePlan(const QString &case_path, const QStringList &no_fly_cells) const {
    QString error;
    const auto case_config = hcore::loadCase(case_path, &error);
    if (!case_config.has_value()) {
        return competition::TaskPlanningResult::failure(error);
    }

    const auto plan = hcore::buildTaskPlan(case_config.value(), no_fly_cells.isEmpty() ? std::nullopt : std::optional<QStringList>(no_fly_cells), &error);
    if (!plan.has_value()) {
        return competition::TaskPlanningResult::failure(error);
    }
    return competition::TaskPlanningResult::success(plan.value());
}
