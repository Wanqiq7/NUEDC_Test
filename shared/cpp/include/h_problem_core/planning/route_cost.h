#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

double estimateMissionTimeSeconds(
    const QStringList &route,
    int height = 7,
    std::optional<LandingProfile> landing_profile = std::nullopt,
    int width = 9,
    QSet<QString> no_fly_cells = {},
    MissionTiming timing = {});

} // namespace hcore
