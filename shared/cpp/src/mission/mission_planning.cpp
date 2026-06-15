#include "h_problem_core/mission/mission_planning.h"

#include "h_problem_core/planning/route_planner.h"

namespace hcore {

std::optional<MissionPlan> buildMissionPlan(
    const CaseConfig &case_config,
    std::optional<QStringList> override_no_fly_cells,
    QString *error_message) {
    MissionPlan plan;
    plan.case_id = case_config.case_id;
    plan.start_cell = case_config.start_cell;
    plan.no_fly_cells = override_no_fly_cells.value_or(case_config.no_fly_cells);

    const QSet<QString> no_fly_set(plan.no_fly_cells.begin(), plan.no_fly_cells.end());
    const std::optional<LandingProfile> landing_profile = case_config.landing;
    const QStringList route = planRoute(
        MapWidth,
        MapHeight,
        case_config.start_cell,
        no_fly_set,
        case_config.return_to_start ? std::optional<QString>(case_config.start_cell) : std::nullopt,
        case_config.return_to_start,
        landing_profile.has_value() ? MissionMode::TimeOptimalOpen : MissionMode::Legacy,
        landing_profile,
        error_message);
    if (route.isEmpty()) {
        if (error_message != nullptr && error_message->isEmpty()) {
            *error_message = QString("Route planner returned empty route for case %1").arg(case_config.case_id);
        }
        return std::nullopt;
    }

    plan.route = route;
    plan.terminal_cell = route.last();
    plan.landing_enabled = case_config.landing.has_value();
    if (case_config.landing.has_value()) {
        plan.descent_angle_deg = case_config.landing->descent_angle_deg;
        plan.takeoff_anchor_x_cm = case_config.landing->takeoff_anchor_cm.x_cm;
        plan.takeoff_anchor_y_cm = case_config.landing->takeoff_anchor_cm.y_cm;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

} // namespace hcore
