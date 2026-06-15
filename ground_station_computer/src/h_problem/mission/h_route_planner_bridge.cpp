#include "h_problem/mission/h_route_planner_bridge.h"

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/mission/mission_plan_store.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace {

MissionPlanData toGroundPlan(const hcore::MissionPlan &plan) {
    MissionPlanData ground_plan;
    ground_plan.case_id = plan.case_id;
    ground_plan.start_cell = plan.start_cell;
    ground_plan.no_fly_cells = plan.no_fly_cells;
    ground_plan.route = plan.route;
    ground_plan.terminal_cell = plan.terminal_cell;
    ground_plan.landing_enabled = plan.landing_enabled;
    ground_plan.descent_angle_deg = plan.descent_angle_deg;
    ground_plan.takeoff_anchor_x_cm = plan.takeoff_anchor_x_cm;
    ground_plan.takeoff_anchor_y_cm = plan.takeoff_anchor_y_cm;
    return ground_plan;
}
} // namespace

MissionPlanResult MissionPlanBridge::parsePlannerOutput(const QByteArray &stdout_bytes) {
    MissionPlanResult result;
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(stdout_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error_message = QString("failed to parse planner output (%1)").arg(parse_error.errorString());
        return result;
    }

    QString error;
    const auto plan = hcore::missionPlanFromJsonObject(document.object(), &error);
    if (!plan.has_value()) {
        result.error_message = error;
        return result;
    }

    result.plan = toGroundPlan(plan.value());
    result.ok = true;
    return result;
}

MissionPlanResult MissionPlanBridge::generatePlan(const QString &case_path, const QStringList &no_fly_cells) const {
    MissionPlanResult result;
    QString error;
    const auto case_config = hcore::loadCase(case_path, &error);
    if (!case_config.has_value()) {
        result.error_message = error;
        return result;
    }

    const auto plan = hcore::buildMissionPlan(case_config.value(), no_fly_cells.isEmpty() ? std::nullopt : std::optional<QStringList>(no_fly_cells), &error);
    if (!plan.has_value()) {
        result.error_message = error;
        return result;
    }

    result.ok = true;
    result.plan = toGroundPlan(plan.value());
    return result;
}
