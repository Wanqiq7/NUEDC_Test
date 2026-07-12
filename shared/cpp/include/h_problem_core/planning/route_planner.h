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
    QStringList route;
    double cost = 0.0;
    double estimated_mission_time_s = 0.0;
    double coverage_rate = 0.0;
    SearchOptimality search_optimality = SearchOptimality::NoFeasibleRoute;
    int search_expansions = 0;
    QStringList warnings;
    QString failure_reason;
};

struct RouteRequest {
    int width = 0;
    int height = 0;
    QString start_cell;
    QSet<QString> no_fly_cells;
    std::optional<LandingProfile> landing_profile = std::nullopt;
    MissionTiming mission_timing;
};

using RoutePlanResult = PlanningResult;

RoutePlanResult planRoute(const RouteRequest &request);

QStringList planRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile,
    QString *error_message = nullptr,
    MissionTiming mission_timing = {});

} // namespace hcore
