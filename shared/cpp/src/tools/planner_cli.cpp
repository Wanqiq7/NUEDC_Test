#include "h_problem_core/tools/planner_cli.h"

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/planning/mission_geometry.h"

#include <nlohmann/json.hpp>

namespace hcore {
namespace {
PlannerCliResult jsonResult(int code, const nlohmann::json &object) { return {code, object.dump(), {}}; }
PlannerCliResult errorResult(int code, const char *error_code, const std::string &message) {
    return jsonResult(code, {{"ok", false}, {"error_code", error_code}, {"message", message}});
}
bool parseStringArray(const nlohmann::json &value, CellList *strings) {
    if (!value.is_array()) return false;
    for (const auto &entry : value) {
        if (!entry.is_string()) return false;
        strings->push_back(entry.get<std::string>());
    }
    return true;
}
bool validNoFlyCell(const std::string &cell) {
    const auto point = decodeCell(cell);
    return point && point->x < MapWidth && point->y < MapHeight;
}
nlohmann::json taskPlanToJson(const TaskPlan &plan) {
    nlohmann::json waypoints = nlohmann::json::array();
    for (const auto &waypoint : plan.waypoints) {
        waypoints.push_back({{"id", waypoint.id}, {"sequence_index", waypoint.sequence_index},
            {"x", waypoint.x}, {"y", waypoint.y}, {"z", waypoint.z},
            {"action", waypoint.action}, {"payload_json", waypoint.payload_json}});
    }
    return {{"message_type", "task_plan"}, {"task_id", plan.task_id}, {"task_type", plan.task_type},
        {"start_waypoint_id", plan.start_waypoint_id}, {"terminal_waypoint_id", plan.terminal_waypoint_id},
        {"waypoints", waypoints}, {"metadata_json", plan.metadata_json}};
}
}

PlannerCliResult runPlannerCliRequest(std::string_view bytes) {
    nlohmann::json request;
    try { request = nlohmann::json::parse(bytes); }
    catch (const nlohmann::json::exception &) { return errorResult(2, "invalid_request", "request must be a JSON object"); }
    if (!request.is_object()) return errorResult(2, "invalid_request", "request must be a JSON object");
    const auto schema = request.find("schema");
    const auto case_path = request.find("case_path");
    if (schema == request.end() || !schema->is_string() || *schema != "h_planning_request_v1"
        || case_path == request.end() || !case_path->is_string() || case_path->get_ref<const std::string &>().empty())
        return errorResult(2, "invalid_request", "invalid schema or case_path");
    const auto no_fly_value = request.find("no_fly_cells");
    CellList no_fly_cells;
    if (no_fly_value == request.end() || !parseStringArray(*no_fly_value, &no_fly_cells))
        return errorResult(2, "invalid_request", "no_fly_cells must be an array of strings");
    for (const auto &cell : no_fly_cells) if (!validNoFlyCell(cell))
        return errorResult(3, "invalid_no_fly_zone", "invalid no-fly cell: " + cell);
    std::string error;
    const auto config = loadCase(case_path->get<std::string>(), &error);
    if (!config) return errorResult(3, "case_load_failed", error);
    const auto plan = buildTaskPlan(*config, no_fly_cells, &error);
    if (!plan) return errorResult(3, "planning_failed", error);
    nlohmann::json metadata;
    try { metadata = nlohmann::json::parse(plan->metadata_json); }
    catch (const nlohmann::json::exception &) { return errorResult(4, "internal_error", "planner returned invalid metadata"); }
    if (!metadata.is_object()) return errorResult(4, "internal_error", "planner returned invalid metadata");
    return jsonResult(0, {{"ok", true}, {"plan", taskPlanToJson(*plan)},
        {"metrics", {{"estimated_mission_time_s", metadata["estimated_mission_time_s"]},
                     {"planning_optimality", metadata["planning_optimality"]}}}});
}
} // namespace hcore
