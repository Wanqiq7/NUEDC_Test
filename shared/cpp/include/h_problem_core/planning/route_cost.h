#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

double estimateMissionTimeSeconds(
    const CellList &route,
    int height = 7,
    std::optional<LandingProfile> landing_profile = std::nullopt,
    int width = 9,
    CellSet no_fly_cells = {},
    MissionTiming timing = {});

} // namespace hcore
