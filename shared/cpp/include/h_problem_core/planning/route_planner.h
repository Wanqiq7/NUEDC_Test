#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

enum class SearchOptimality {
    ProvenOptimal,
    BestEffort,
    SearchLimitReached,
    NoFeasibleRoute,
};

struct PlanningResult {
    bool ok = false;
    CellList route;
    double cost = 0.0;
    double estimated_mission_time_s = 0.0;
    double coverage_rate = 0.0;
    SearchOptimality search_optimality = SearchOptimality::NoFeasibleRoute;
    int search_expansions = 0;
    CellList warnings;
    std::string failure_reason;
};

struct RouteRequest {
    int width = 0;
    int height = 0;
    std::string start_cell;
    CellSet no_fly_cells;
    std::optional<LandingProfile> landing_profile = std::nullopt;
    MissionTiming mission_timing;
};

using RoutePlanResult = PlanningResult;

RoutePlanResult planRoute(const RouteRequest &request);

CellList planRoute(
    int width,
    int height,
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile,
    std::string *error_message = nullptr,
    MissionTiming mission_timing = {});

} // namespace hcore
