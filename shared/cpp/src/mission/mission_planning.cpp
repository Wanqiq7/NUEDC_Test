#include "h_problem_core/mission/mission_planning.h"

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_planner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hcore {

namespace {

QString planningOptimalityName(SearchOptimality optimality) {
    switch (optimality) {
    case SearchOptimality::ProvenOptimal:
        return "proven_optimal";
    case SearchOptimality::BestEffort:
        return "best_effort";
    case SearchOptimality::SearchLimitReached:
        return "search_limit_reached";
    case SearchOptimality::NoFeasibleRoute:
        return "no_feasible_route";
    }
    return "no_feasible_route";
}

}

QJsonArray stringListToJsonArray(const QStringList &strings) {
    QJsonArray array;
    for (const QString &value : strings) {
        array.append(value);
    }
    return array;
}

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<competition::TaskPlan> buildTaskPlan(
    const CaseConfig &case_config,
    std::optional<QStringList> override_no_fly_cells,
    QString *error_message) {
    const QStringList no_fly_cells = override_no_fly_cells.value_or(case_config.no_fly_cells);

    const QSet<QString> no_fly_set(no_fly_cells.begin(), no_fly_cells.end());
    if (!case_config.landing.has_value()) {
        if (error_message != nullptr) {
            *error_message = "landing profile is required for H mission planning";
        }
        return std::nullopt;
    }
    RouteRequest request;
    request.width = MapWidth;
    request.height = MapHeight;
    request.start_cell = case_config.start_cell;
    request.no_fly_cells = no_fly_set;
    request.landing_profile = case_config.landing;
    request.mission_timing = case_config.mission_timing;

    const RoutePlanResult route_result = planRoute(request);
    if (!route_result.ok) {
        if (error_message != nullptr && error_message->isEmpty()) {
            *error_message = route_result.failure_reason.isEmpty()
                ? QString("Route planner returned empty route for case %1").arg(case_config.case_id)
                : route_result.failure_reason;
        }
        return std::nullopt;
    }

    const auto landing_approach = landingApproachForTerminal(
        MapWidth,
        MapHeight,
        route_result.route.last(),
        no_fly_set,
        case_config.landing.value());
    if (!landing_approach.has_value()) {
        if (error_message != nullptr) {
            *error_message = "planner returned a terminal without a validated landing approach";
        }
        return std::nullopt;
    }
    const auto terminal_center = cellCodeCenterCm(route_result.route.last(), MapHeight);
    if (!terminal_center.has_value()) {
        if (error_message != nullptr) {
            *error_message = "planner returned an invalid landing terminal";
        }
        return std::nullopt;
    }
    QJsonObject metadata;
    metadata["case_id"] = case_config.case_id;
    metadata["start_cell"] = case_config.start_cell;
    metadata["no_fly_cells"] = stringListToJsonArray(no_fly_cells);
    metadata["terminal_cell"] = route_result.route.last();
    metadata["landing_enabled"] = true;
    metadata["descent_angle_deg"] = case_config.landing->descent_angle_deg;
    metadata["takeoff_anchor_x_cm"] = case_config.landing->takeoff_anchor_cm.x_cm;
    metadata["takeoff_anchor_y_cm"] = case_config.landing->takeoff_anchor_cm.y_cm;
    metadata["touchdown_x_cm"] = landing_approach->touchdown_point_cm.x_cm;
    metadata["touchdown_y_cm"] = landing_approach->touchdown_point_cm.y_cm;
    metadata["descent_run_cm"] = landing_approach->horizontal_run_cm;
    metadata["descent_heading_deg"] = headingDegrees(
        terminal_center.value(), landing_approach->touchdown_point_cm);
    metadata["estimated_mission_time_s"] = route_result.estimated_mission_time_s;
    metadata["planning_optimality"] = planningOptimalityName(route_result.search_optimality);
    metadata["planning_warnings"] = stringListToJsonArray(route_result.warnings);

    competition::TaskPlan plan;
    plan.task_id = case_config.case_id;
    plan.task_type = "h_problem";
    plan.start_waypoint_id = case_config.start_cell;
    plan.terminal_waypoint_id = route_result.route.last();
    plan.metadata_json = compactJson(metadata);
    for (int index = 0; index < route_result.route.size(); ++index) {
        competition::TaskWaypoint waypoint;
        waypoint.id = route_result.route.at(index);
        waypoint.sequence_index = static_cast<quint32>(index);
        waypoint.action = "navigate";
        const auto center = cellCodeCenterCm(waypoint.id, MapHeight);
        if (center.has_value()) {
            waypoint.x = center->x_cm;
            waypoint.y = center->y_cm;
        }
        waypoint.payload_json = compactJson(QJsonObject{{"cell", waypoint.id}});
        plan.waypoints.append(waypoint);
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

} // namespace hcore
