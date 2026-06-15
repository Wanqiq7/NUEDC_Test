#include "h_problem_core/planning/route_cost.h"

#include "h_problem_core/planning/mission_geometry.h"

namespace hcore {

namespace {

QPoint stepVector(const QString &from_cell, const QString &to_cell) {
    const auto from = decodeCell(from_cell);
    const auto to = decodeCell(to_cell);
    if (!from.has_value() || !to.has_value()) {
        return {};
    }
    return {to->x() - from->x(), to->y() - from->y()};
}

}

int countHeadingChanges(const QStringList &route) {
    int heading_changes = 0;
    std::optional<QPoint> previous_vector;
    for (int index = 1; index < route.size(); ++index) {
        const QPoint current_vector = stepVector(route.at(index - 1), route.at(index));
        if (previous_vector.has_value() && previous_vector.value() != current_vector) {
            ++heading_changes;
        }
        previous_vector = current_vector;
    }
    return heading_changes;
}

double estimateRouteCost(
    const QStringList &route,
    int height,
    double turn_penalty_cm,
    double repeated_cell_penalty_cm,
    std::optional<LandingProfile> landing_profile,
    int width,
    QSet<QString> no_fly_cells) {
    if (route.size() <= 1) {
        return 0.0;
    }

    double distance_cost = 0.0;
    QMap<QString, int> visit_counts;
    for (int index = 1; index < route.size(); ++index) {
        const auto previous_center = cellCodeCenterCm(route.at(index - 1), height);
        const auto current_center = cellCodeCenterCm(route.at(index), height);
        if (previous_center.has_value() && current_center.has_value()) {
            distance_cost += euclideanDistanceCm(previous_center.value(), current_center.value());
        }
        visit_counts[route.at(index)] = visit_counts.value(route.at(index)) + 1;
    }

    int repeat_count = 0;
    for (const int count : visit_counts) {
        repeat_count += std::max(count - 1, 0);
    }

    double total_cost = distance_cost
        + (repeat_count * repeated_cell_penalty_cm)
        + (countHeadingChanges(route) * turn_penalty_cm);

    if (landing_profile.has_value()) {
        const auto terminal_center = cellCodeCenterCm(route.last(), height);
        if (terminal_center.has_value()) {
            total_cost += euclideanDistanceCm(terminal_center.value(), landing_profile->takeoff_anchor_cm);
        }
        const QSet<QString> landing_cells = terminalCellsForLanding(width, height, no_fly_cells, landing_profile.value());
        if (!landing_cells.contains(route.last())) {
            total_cost += 10000.0;
        }
    }

    return total_cost;
}

} // namespace hcore
