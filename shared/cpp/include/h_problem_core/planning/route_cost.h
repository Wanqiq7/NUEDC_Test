#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

int countHeadingChanges(const QStringList &route);
double estimateRouteCost(
    const QStringList &route,
    int height = 7,
    double turn_penalty_cm = 18.0,
    double repeated_cell_penalty_cm = 6.0,
    std::optional<LandingProfile> landing_profile = std::nullopt,
    int width = 9,
    QSet<QString> no_fly_cells = {});

} // namespace hcore
