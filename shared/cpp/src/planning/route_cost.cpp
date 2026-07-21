#include "h_problem_core/planning/route_cost.h"
#include "h_problem_core/planning/mission_geometry.h"

#include <cmath>
#include <limits>

namespace hcore {
namespace {
bool timingIsValid(const MissionTiming &timing) {
    return std::isfinite(timing.cruise_speed_cm_per_s) && std::isfinite(timing.ascent_speed_cm_per_s)
        && std::isfinite(timing.descent_speed_cm_per_s) && std::isfinite(timing.takeoff_fixed_time_s)
        && std::isfinite(timing.landing_fixed_time_s) && std::isfinite(timing.per_cell_dwell_time_s)
        && timing.cruise_speed_cm_per_s > 0.0 && timing.ascent_speed_cm_per_s > 0.0
        && timing.descent_speed_cm_per_s > 0.0 && timing.takeoff_fixed_time_s >= 0.0
        && timing.landing_fixed_time_s >= 0.0 && timing.per_cell_dwell_time_s >= 0.0;
}
double routeDistanceCm(const CellList &route, int height) {
    double distance = 0.0;
    for (std::size_t index = 1; index < route.size(); ++index) {
        const auto previous = cellCodeCenterCm(route[index - 1], height);
        const auto current = cellCodeCenterCm(route[index], height);
        if (previous && current) distance += euclideanDistanceCm(*previous, *current);
    }
    return distance;
}
}

double estimateMissionTimeSeconds(const CellList &route, int height,
        std::optional<LandingProfile> landing_profile, int width, CellSet no_fly_cells,
        MissionTiming timing) {
    if (route.empty() || !timingIsValid(timing)) return route.empty() ? 0.0 : std::numeric_limits<double>::infinity();
    const CellSet visited(route.begin(), route.end());
    double total = timing.takeoff_fixed_time_s + routeDistanceCm(route, height) / timing.cruise_speed_cm_per_s
        + visited.size() * timing.per_cell_dwell_time_s;
    if (!landing_profile) return total;
    const auto start = cellCodeCenterCm(route.front(), height);
    if (!start || !descentCorridorIsClear(width, height, landing_profile->takeoff_anchor_cm, *start, no_fly_cells))
        return std::numeric_limits<double>::infinity();
    total += euclideanDistanceCm(landing_profile->takeoff_anchor_cm, *start) / timing.cruise_speed_cm_per_s;
    const auto approach = landingApproachForTerminal(width, height, route.back(), no_fly_cells, *landing_profile);
    if (!approach) return std::numeric_limits<double>::infinity();
    return total + landing_profile->cruise_height_cm / timing.ascent_speed_cm_per_s
        + approach->descent_distance_cm / timing.descent_speed_cm_per_s + timing.landing_fixed_time_s;
}
} // namespace hcore
