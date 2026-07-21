#include "h_problem_core/mission/mission_planning.h"

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_planner.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace hcore {
namespace {
nlohmann::json compactNumber(double value) {
    if (std::isfinite(value) && std::floor(value) == value
        && value >= static_cast<double>(std::numeric_limits<std::int64_t>::min())
        && value <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(value);
    }
    return value;
}

std::string planningOptimalityName(SearchOptimality optimality) {
    switch (optimality) {
    case SearchOptimality::ProvenOptimal: return "proven_optimal";
    case SearchOptimality::BestEffort: return "best_effort";
    case SearchOptimality::SearchLimitReached: return "search_limit_reached";
    case SearchOptimality::NoFeasibleRoute: return "no_feasible_route";
    }
    return "no_feasible_route";
}
}

std::optional<TaskPlan> buildTaskPlan(const CaseConfig &case_config,
        std::optional<CellList> override_no_fly_cells, std::string *error) {
    const CellList no_fly_cells = override_no_fly_cells.value_or(case_config.no_fly_cells);
    const CellSet no_fly_set(no_fly_cells.begin(), no_fly_cells.end());
    if (!case_config.landing) { if (error) *error = "landing profile is required for H mission planning"; return std::nullopt; }
    if (case_config.start_cell != "A9B1") { if (error) *error = "H mission start_cell must be A9B1"; return std::nullopt; }
    RouteRequest request{MapWidth, MapHeight, case_config.start_cell, no_fly_set,
        case_config.landing, case_config.mission_timing};
    const auto route_result = planRoute(request);
    if (!route_result.ok) {
        if (error && error->empty()) *error = route_result.failure_reason.empty()
            ? "Route planner returned empty route for case " + case_config.case_id : route_result.failure_reason;
        return std::nullopt;
    }
    const auto approach = landingApproachForTerminal(MapWidth, MapHeight, route_result.route.back(),
        no_fly_set, *case_config.landing);
    if (!approach) { if (error) *error = "planner returned a terminal without a validated landing approach"; return std::nullopt; }
    const auto terminal_center = cellCodeCenterCm(route_result.route.back(), MapHeight);
    if (!terminal_center) { if (error) *error = "planner returned an invalid landing terminal"; return std::nullopt; }

    nlohmann::json metadata{
        {"case_id", case_config.case_id}, {"start_cell", case_config.start_cell},
        {"no_fly_cells", no_fly_cells}, {"terminal_cell", route_result.route.back()},
        {"landing_enabled", true}, {"descent_angle_deg", compactNumber(case_config.landing->descent_angle_deg)},
        {"takeoff_anchor_x_cm", compactNumber(case_config.landing->takeoff_anchor_cm.x_cm)},
        {"takeoff_anchor_y_cm", compactNumber(case_config.landing->takeoff_anchor_cm.y_cm)},
        {"touchdown_x_cm", compactNumber(approach->touchdown_point_cm.x_cm)},
        {"touchdown_y_cm", compactNumber(approach->touchdown_point_cm.y_cm)},
        {"descent_run_cm", compactNumber(approach->horizontal_run_cm)},
        {"descent_heading_deg", compactNumber(headingDegrees(*terminal_center, approach->touchdown_point_cm))},
        {"estimated_mission_time_s", compactNumber(route_result.estimated_mission_time_s)},
        {"planning_optimality", planningOptimalityName(route_result.search_optimality)},
        {"planning_warnings", route_result.warnings}, {"execution_contract", HExecutionContract},
        {"cruise_height_cm", compactNumber(case_config.landing->cruise_height_cm)},
    };

    TaskPlan plan;
    plan.task_id = case_config.case_id;
    plan.task_type = "h_problem";
    plan.start_waypoint_id = case_config.start_cell;
    plan.metadata_json = metadata.dump();
    const double cruise_height_m = case_config.landing->cruise_height_cm / 100.0;
    for (std::size_t index = 0; index < route_result.route.size(); ++index) {
        const auto &cell = route_result.route[index];
        const auto center = cellCodeCenterCm(cell, MapHeight);
        if (!center) { if (error) *error = "planner returned invalid route cell: " + cell; return std::nullopt; }
        const auto point = fieldPointToMissionMeters(*center);
        plan.waypoints.push_back({cell, static_cast<std::uint32_t>(index), point.x_m, point.y_m,
            cruise_height_m, index == 0 ? "takeoff" : "navigate", nlohmann::json{{"cell", cell}}.dump()});
    }
    const auto touchdown = fieldPointToMissionMeters(approach->touchdown_point_cm);
    plan.waypoints.push_back({HTouchdownWaypointId, static_cast<std::uint32_t>(plan.waypoints.size()),
        touchdown.x_m, touchdown.y_m, 0.0, "land", nlohmann::json{{"touchdown", true}}.dump()});
    plan.terminal_waypoint_id = HTouchdownWaypointId;
    if (error) error->clear();
    return plan;
}
} // namespace hcore
