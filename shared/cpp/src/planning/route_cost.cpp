#include "h_problem_core/planning/route_cost.h"

#include "h_problem_core/planning/mission_geometry.h"

#include <cmath>
#include <limits>

namespace hcore {

namespace {

bool timingIsValid(const MissionTiming &timing) {
    return std::isfinite(timing.cruise_speed_cm_per_s)
        && std::isfinite(timing.ascent_speed_cm_per_s)
        && std::isfinite(timing.descent_speed_cm_per_s)
        && std::isfinite(timing.takeoff_fixed_time_s)
        && std::isfinite(timing.landing_fixed_time_s)
        && std::isfinite(timing.per_cell_dwell_time_s)
        && timing.cruise_speed_cm_per_s > 0.0
        && timing.ascent_speed_cm_per_s > 0.0
        && timing.descent_speed_cm_per_s > 0.0
        && timing.takeoff_fixed_time_s >= 0.0
        && timing.landing_fixed_time_s >= 0.0
        && timing.per_cell_dwell_time_s >= 0.0;
}

double routeDistanceCm(const QStringList &route, int height) {
    double distance_cm = 0.0;
    for (int index = 1; index < route.size(); ++index) {
        const auto previous_center = cellCodeCenterCm(route.at(index - 1), height);
        const auto current_center = cellCodeCenterCm(route.at(index), height);
        if (previous_center.has_value() && current_center.has_value()) {
            distance_cm += euclideanDistanceCm(previous_center.value(), current_center.value());
        }
    }
    return distance_cm;
}

}

double estimateMissionTimeSeconds(
    const QStringList &route,
    int height,
    std::optional<LandingProfile> landing_profile,
    int width,
    QSet<QString> no_fly_cells,
    MissionTiming timing) {
    if (route.isEmpty() || !timingIsValid(timing)) {
        return route.isEmpty() ? 0.0 : std::numeric_limits<double>::infinity();
    }

    const QSet<QString> visited_cells(route.begin(), route.end());
    double total_time_s = timing.takeoff_fixed_time_s
        + (routeDistanceCm(route, height) / timing.cruise_speed_cm_per_s)
        + (visited_cells.size() * timing.per_cell_dwell_time_s);

    if (!landing_profile.has_value()) {
        return total_time_s;
    }

    const auto start_center = cellCodeCenterCm(route.first(), height);
    if (!start_center.has_value()
        || !descentCorridorIsClear(
            width,
            height,
            landing_profile->takeoff_anchor_cm,
            start_center.value(),
            no_fly_cells)) {
        return std::numeric_limits<double>::infinity();
    }

    total_time_s += euclideanDistanceCm(
        landing_profile->takeoff_anchor_cm,
        start_center.value()) / timing.cruise_speed_cm_per_s;
    const auto approach = landingApproachForTerminal(
        width,
        height,
        route.last(),
        no_fly_cells,
        landing_profile.value());
    if (!approach.has_value()) {
        return std::numeric_limits<double>::infinity();
    }

    total_time_s += landing_profile->cruise_height_cm / timing.ascent_speed_cm_per_s;
    total_time_s += approach->descent_distance_cm / timing.descent_speed_cm_per_s;
    total_time_s += timing.landing_fixed_time_s;
    return total_time_s;
}

} // namespace hcore
