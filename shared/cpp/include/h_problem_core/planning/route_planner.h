#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

enum class MissionMode {
    Legacy,
    TimeOptimalOpen,
};

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

QStringList exactCompletionToEndForTesting(
    int width,
    int height,
    const QString &current_cell,
    const QStringList &required_cells,
    const QString &end_cell,
    const QSet<QString> &no_fly_cells);

} // namespace hcore
