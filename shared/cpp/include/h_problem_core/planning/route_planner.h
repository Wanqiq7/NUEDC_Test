#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

enum class MissionMode {
    Legacy,
    TimeOptimalOpen,
};

struct PlanningResult {
    bool ok = false;
    QStringList route;
    double cost = 0.0;
    double coverage_rate = 0.0;
    QStringList warnings;
    QString failure_reason;
};

struct RouteRequest {
    int width = 0;
    int height = 0;
    QString start_cell;
    QSet<QString> no_fly_cells;
    std::optional<QString> end_cell = std::nullopt;
    bool require_cycle = false;
    MissionMode mission_mode = MissionMode::Legacy;
    std::optional<LandingProfile> landing_profile = std::nullopt;
};

using RoutePlanResult = PlanningResult;

RoutePlanResult planRoute(const RouteRequest &request);

QStringList planRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    std::optional<QString> end_cell = std::nullopt,
    bool require_cycle = false,
    MissionMode mission_mode = MissionMode::Legacy,
    std::optional<LandingProfile> landing_profile = std::nullopt,
    QString *error_message = nullptr);

PlanningResult planRouteWithDetails(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    std::optional<QString> end_cell = std::nullopt,
    bool require_cycle = false,
    MissionMode mission_mode = MissionMode::Legacy,
    std::optional<LandingProfile> landing_profile = std::nullopt);

QStringList exactCompletionToEndForTesting(
    int width,
    int height,
    const QString &current_cell,
    const QStringList &required_cells,
    const QString &end_cell,
    const QSet<QString> &no_fly_cells);

} // namespace hcore
