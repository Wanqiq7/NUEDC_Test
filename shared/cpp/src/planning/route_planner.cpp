#include "h_problem_core/planning/route_planner.h"

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"

#include <QHash>
#include <QQueue>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace hcore {

namespace {

QStringList gridNeighbors(const QString &cell, int width, int height, const QSet<QString> &no_fly_cells) {
    const auto decoded = decodeCell(cell);
    if (!decoded.has_value()) {
        return {};
    }
    const int x_index = decoded->x();
    const int y_index = decoded->y();
    const QVector<QPoint> candidates{
        {x_index + 1, y_index},
        {x_index - 1, y_index},
        {x_index, y_index + 1},
        {x_index, y_index - 1},
    };

    QStringList neighbors;
    for (const QPoint &candidate : candidates) {
        if (candidate.x() < 0 || candidate.x() >= width || candidate.y() < 0 || candidate.y() >= height) {
            continue;
        }
        const QString next_cell = encodeCell(candidate.x(), candidate.y());
        if (!no_fly_cells.contains(next_cell)) {
            neighbors.append(next_cell);
        }
    }
    return neighbors;
}

QStringList shortestPath(int width, int height, const QString &start_cell, const QString &end_cell, const QSet<QString> &no_fly_cells) {
    QQueue<QString> queue;
    queue.enqueue(start_cell);
    QMap<QString, QString> parent;
    QSet<QString> has_parent{start_cell};

    while (!queue.isEmpty()) {
        const QString cell = queue.dequeue();
        if (cell == end_cell) {
            break;
        }
        for (const QString &neighbor : gridNeighbors(cell, width, height, no_fly_cells)) {
            if (has_parent.contains(neighbor)) {
                continue;
            }
            has_parent.insert(neighbor);
            parent.insert(neighbor, cell);
            queue.enqueue(neighbor);
        }
    }

    if (!has_parent.contains(end_cell)) {
        return {};
    }

    QStringList path;
    QString current = end_cell;
    while (current != start_cell) {
        path.prepend(current);
        current = parent.value(current);
    }
    path.prepend(start_cell);
    return path;
}

int countBits(quint64 value) {
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

int minimumRepeatCountForEndpoint(
    int even_cells,
    int odd_cells,
    bool start_is_even,
    bool end_is_even,
    int total_cells) {
    int minimum_nodes = 0;
    if (start_is_even == end_is_even) {
        const int start_color_cells = start_is_even ? even_cells : odd_cells;
        const int other_color_cells = start_is_even ? odd_cells : even_cells;
        minimum_nodes = std::max((2 * start_color_cells) - 1, (2 * other_color_cells) + 1);
    } else {
        minimum_nodes = 2 * std::max(even_cells, odd_cells);
    }
    return std::max(0, minimum_nodes - total_cells);
}

bool isBetterMissionRoute(
    const QStringList &candidate,
    const QStringList &current_best,
    int width,
    int height,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    if (current_best.isEmpty()) {
        return true;
    }

    const double candidate_time_s = estimateMissionTimeSeconds(
        candidate, height, landing_profile, width, no_fly_cells, mission_timing);
    const double current_time_s = estimateMissionTimeSeconds(
        current_best, height, landing_profile, width, no_fly_cells, mission_timing);
    if (std::abs(candidate_time_s - current_time_s) > 1e-9) {
        return candidate_time_s < current_time_s;
    }
    if (candidate.size() != current_best.size()) {
        return candidate.size() < current_best.size();
    }
    return candidate < current_best;
}

struct CoverageSearchResult {
    QStringList route;
    int expansions = 0;
    bool proven_optimal = false;
    bool search_limit_reached = false;
    bool stopped_after_feasible_route = false;
};

CoverageSearchResult planBoundedCoverageRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const QStringList &terminal_cells,
    const QStringList &incumbent_route,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing,
    int expansion_limit) {
    struct Grid {
        QVector<QString> cells;
        QMap<QString, int> index_by_cell;
        QVector<QVector<int>> neighbors;
        QVector<QVector<int>> distances;
        quint64 full_mask = 0;
        int even_cells = 0;
        int odd_cells = 0;
    } grid;

    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (no_fly_cells.contains(cell)) {
                continue;
            }
            const int index = grid.cells.size();
            grid.cells.append(cell);
            grid.index_by_cell.insert(cell, index);
            if ((x_index + y_index) % 2 == 0) {
                ++grid.even_cells;
            } else {
                ++grid.odd_cells;
            }
        }
    }

    const int total_cells = grid.cells.size();
    if (total_cells == 0 || total_cells > 63 || !grid.index_by_cell.contains(start_cell)) {
        return {};
    }
    grid.full_mask = (quint64{1} << total_cells) - 1;

    grid.neighbors.resize(total_cells);
    for (int index = 0; index < total_cells; ++index) {
        const QPoint point = decodeCell(grid.cells.at(index)).value();
        const QVector<QPoint> candidates{
            {point.x() + 1, point.y()},
            {point.x() - 1, point.y()},
            {point.x(), point.y() + 1},
            {point.x(), point.y() - 1},
        };
        for (const QPoint &candidate : candidates) {
            const QString neighbor = encodeCell(candidate.x(), candidate.y());
            if (grid.index_by_cell.contains(neighbor)) {
                grid.neighbors[index].append(grid.index_by_cell.value(neighbor));
            }
        }
    }

    grid.distances.resize(total_cells);
    for (int source = 0; source < total_cells; ++source) {
        QVector<int> &distances = grid.distances[source];
        distances.fill(-1, total_cells);
        QQueue<int> queue;
        distances[source] = 0;
        queue.enqueue(source);
        while (!queue.isEmpty()) {
            const int cell = queue.dequeue();
            for (const int neighbor : grid.neighbors.at(cell)) {
                if (distances.at(neighbor) >= 0) {
                    continue;
                }
                distances[neighbor] = distances.at(cell) + 1;
                queue.enqueue(neighbor);
            }
        }
    }

    const int start_index = grid.index_by_cell.value(start_cell);
    const auto start_center = cellCodeCenterCm(start_cell, height);
    if (!start_center.has_value()
        || !descentCorridorIsClear(
            width,
            height,
            landing_profile.takeoff_anchor_cm,
            start_center.value(),
            no_fly_cells)) {
        return {};
    }
    const double takeoff_transit_time_s = euclideanDistanceCm(
        landing_profile.takeoff_anchor_cm,
        start_center.value()) / mission_timing.cruise_speed_cm_per_s;
    const QPoint start_point = decodeCell(start_cell).value();
    const bool start_is_even = (start_point.x() + start_point.y()) % 2 == 0;
    QStringList ordered_terminals = terminal_cells;
    std::sort(ordered_terminals.begin(), ordered_terminals.end(), [&](const QString &left, const QString &right) {
        const QPoint left_point = decodeCell(left).value();
        const QPoint right_point = decodeCell(right).value();
        const bool left_matches_start = ((left_point.x() + left_point.y()) % 2 == 0) == start_is_even;
        const bool right_matches_start = ((right_point.x() + right_point.y()) % 2 == 0) == start_is_even;
        return std::make_tuple(!left_matches_start, left) < std::make_tuple(!right_matches_start, right);
    });
    QStringList best_route = incumbent_route;
    double best_time_s = best_route.isEmpty()
        ? std::numeric_limits<double>::infinity()
        : estimateMissionTimeSeconds(best_route, height, landing_profile, width, no_fly_cells, mission_timing);
    int expansions = 0;
    bool search_limit_reached = false;
    bool stopped_after_feasible_route = false;
    const int terminal_count = std::max(1, static_cast<int>(ordered_terminals.size()));
    const int expansion_budget_per_terminal = std::max(1, expansion_limit / terminal_count);

    for (const QString &terminal_cell : ordered_terminals) {
        if (expansions >= expansion_limit || !grid.index_by_cell.contains(terminal_cell)) {
            break;
        }
        const int terminal_index = grid.index_by_cell.value(terminal_cell);
        if (grid.distances.at(start_index).at(terminal_index) < 0) {
            continue;
        }
        const QPoint terminal_point = decodeCell(terminal_cell).value();
        const bool terminal_is_even = (terminal_point.x() + terminal_point.y()) % 2 == 0;
        const int minimum_repeats = minimumRepeatCountForEndpoint(
            grid.even_cells,
            grid.odd_cells,
            start_is_even,
            terminal_is_even,
            total_cells);
        const auto landing_approach = landingApproachForTerminal(
            width,
            height,
            terminal_cell,
            no_fly_cells,
            landing_profile);
        if (!landing_approach.has_value()) {
            continue;
        }
        const double terminal_fixed_time_s = mission_timing.takeoff_fixed_time_s
            + takeoff_transit_time_s
            + (landing_profile.cruise_height_cm / mission_timing.ascent_speed_cm_per_s)
            + (landing_approach->descent_distance_cm / mission_timing.descent_speed_cm_per_s)
            + mission_timing.landing_fixed_time_s
            + (total_cells * mission_timing.per_cell_dwell_time_s);
        const int terminal_expansion_limit = std::min(expansion_limit, expansions + expansion_budget_per_terminal);

        const auto maximumRepeatBudget = [&] {
            if (!std::isfinite(best_time_s)) {
                return total_cells;
            }
            const double available_xy_time_s = best_time_s - terminal_fixed_time_s;
            const int maximum_steps = static_cast<int>(std::floor(
                (available_xy_time_s * mission_timing.cruise_speed_cm_per_s / CellSizeCm) + 1e-9));
            return std::max(-1, maximum_steps - (total_cells - 1));
        };

        for (int repeat_cap = minimum_repeats;
             repeat_cap <= maximumRepeatBudget() && expansions < terminal_expansion_limit;
             ++repeat_cap) {
            struct StateKey {
                quint64 visited_mask = 0;
                int position = 0;

                bool operator<(const StateKey &other) const {
                    return std::tie(visited_mask, position)
                        < std::tie(other.visited_mask, other.position);
                }
            };
            struct Candidate {
                int index = 0;
                bool is_repeat = false;
                int early_terminal = 0;
                int onward_unvisited_degree = 0;
                int distance_to_terminal = 0;
                int sweep_rank = 0;
            };

            std::map<StateKey, int> best_repeat_counts;
            QVector<int> path{start_index};
            const quint64 start_mask = quint64{1} << start_index;
            bool terminal_route_found = false;
            std::function<void(int, quint64, int)> dfs =
                [&](int position, quint64 visited_mask, int repeats) {
                    if (terminal_route_found) {
                        return;
                    }
                    if (expansions >= terminal_expansion_limit) {
                        search_limit_reached = true;
                        return;
                    }
                    ++expansions;

                    const int covered = countBits(visited_mask);
                    const int remaining = total_cells - covered;
                    const int distance_to_terminal = grid.distances.at(position).at(terminal_index);
                    if (distance_to_terminal < 0) {
                        return;
                    }
                    const int minimum_additional_steps = std::max(remaining, distance_to_terminal);
                    const double minimum_time_s = (
                        (path.size() - 1 + minimum_additional_steps) * CellSizeCm / mission_timing.cruise_speed_cm_per_s)
                        + terminal_fixed_time_s;
                    if (minimum_time_s > best_time_s + 1e-9) {
                        return;
                    }
                    if (visited_mask == grid.full_mask && position == terminal_index) {
                        QStringList found_route;
                        for (const int index : path) {
                            found_route.append(grid.cells.at(index));
                        }
                        if (isBetterMissionRoute(
                                found_route,
                                best_route,
                                width,
                                height,
                                no_fly_cells,
                                landing_profile,
                                mission_timing)) {
                            best_route = found_route;
                            best_time_s = estimateMissionTimeSeconds(
                                best_route,
                                height,
                                landing_profile,
                                width,
                                no_fly_cells,
                                mission_timing);
                        }
                        terminal_route_found = true;
                        return;
                    }

                    const StateKey state{visited_mask, position};
                    const auto existing = best_repeat_counts.find(state);
                    if (existing != best_repeat_counts.end() && existing->second <= repeats) {
                        return;
                    }
                    best_repeat_counts[state] = repeats;

                    QVector<Candidate> candidates;
                    for (const int neighbor : grid.neighbors.at(position)) {
                        const bool is_repeat = (visited_mask & (quint64{1} << neighbor)) != 0;
                        if (is_repeat && repeats >= repeat_cap) {
                            continue;
                        }
                        int onward_unvisited_degree = 0;
                        for (const int next_neighbor : grid.neighbors.at(neighbor)) {
                            if ((visited_mask & (quint64{1} << next_neighbor)) == 0) {
                                ++onward_unvisited_degree;
                            }
                        }
                        const QPoint neighbor_point = decodeCell(grid.cells.at(neighbor)).value();
                        const int sweep_rank = (neighbor_point.y() * width)
                            + (neighbor_point.y() % 2 == 0
                                   ? neighbor_point.x()
                                   : (width - 1 - neighbor_point.x()));
                        candidates.append({
                            neighbor,
                            is_repeat,
                            neighbor == terminal_index && visited_mask != grid.full_mask ? 1 : 0,
                            onward_unvisited_degree,
                            grid.distances.at(neighbor).at(terminal_index),
                            sweep_rank,
                        });
                    }
                    const int unvisited_leaf_choices = std::count_if(
                        candidates.begin(),
                        candidates.end(),
                        [](const Candidate &candidate) {
                            return !candidate.is_repeat && candidate.onward_unvisited_degree == 0;
                        });
                    std::sort(candidates.begin(), candidates.end(), [&](const Candidate &left, const Candidate &right) {
                        return std::make_tuple(
                            left.is_repeat,
                            left.early_terminal,
                            left.onward_unvisited_degree == 0 && unvisited_leaf_choices > 1,
                            left.onward_unvisited_degree,
                            -left.distance_to_terminal,
                            left.sweep_rank,
                            grid.cells.at(left.index))
                            < std::make_tuple(
                                right.is_repeat,
                                right.early_terminal,
                            right.onward_unvisited_degree == 0 && unvisited_leaf_choices > 1,
                            right.onward_unvisited_degree,
                            -right.distance_to_terminal,
                            right.sweep_rank,
                            grid.cells.at(right.index));
                    });

                    for (const Candidate &candidate : candidates) {
                        const quint64 next_mask = visited_mask | (quint64{1} << candidate.index);
                        path.append(candidate.index);
                        dfs(candidate.index, next_mask, repeats + (candidate.is_repeat ? 1 : 0));
                        path.removeLast();
                    }
                };

            dfs(start_index, start_mask, 0);
            if (terminal_route_found) {
                stopped_after_feasible_route = true;
                break;
            }
        }
    }

    return {
        best_route,
        expansions,
        !search_limit_reached && !stopped_after_feasible_route,
        search_limit_reached,
        stopped_after_feasible_route,
    };
}

CoverageSearchResult planExactCoverageRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const QStringList &terminal_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    constexpr int ExactCoverageCellLimit = 16;
    constexpr int StatePositionBits = 6;
    constexpr quint64 StatePositionMask = (quint64{1} << StatePositionBits) - 1;

    QVector<QString> cells;
    QMap<QString, int> index_by_cell;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (no_fly_cells.contains(cell)) {
                continue;
            }
            index_by_cell.insert(cell, cells.size());
            cells.append(cell);
        }
    }
    if (cells.isEmpty()
        || cells.size() > ExactCoverageCellLimit
        || !index_by_cell.contains(start_cell)) {
        return {};
    }

    QVector<QVector<int>> neighbors(cells.size());
    for (int index = 0; index < cells.size(); ++index) {
        const QPoint point = decodeCell(cells.at(index)).value();
        for (const QPoint &candidate : QVector<QPoint>{
                 {point.x() + 1, point.y()},
                 {point.x() - 1, point.y()},
                 {point.x(), point.y() + 1},
                 {point.x(), point.y() - 1},
             }) {
            const QString neighbor = encodeCell(candidate.x(), candidate.y());
            if (index_by_cell.contains(neighbor)) {
                neighbors[index].append(index_by_cell.value(neighbor));
            }
        }
    }

    QSet<int> unresolved_terminals;
    for (const QString &terminal : terminal_cells) {
        if (index_by_cell.contains(terminal)) {
            unresolved_terminals.insert(index_by_cell.value(terminal));
        }
    }
    if (unresolved_terminals.isEmpty()) {
        return {};
    }

    const auto stateKey = [](quint64 visited_mask, int position) {
        return (visited_mask << StatePositionBits) | static_cast<quint64>(position);
    };
    const int start_index = index_by_cell.value(start_cell);
    const quint64 start_mask = quint64{1} << start_index;
    const quint64 full_mask = (quint64{1} << cells.size()) - 1;
    const quint64 start_state = stateKey(start_mask, start_index);
    QHash<quint64, quint64> parent_by_state;
    parent_by_state.insert(start_state, start_state);
    QQueue<quint64> queue;
    queue.enqueue(start_state);
    QMap<int, quint64> terminal_state_by_index;
    int expansions = 0;

    while (!queue.isEmpty()) {
        const quint64 state = queue.dequeue();
        ++expansions;
        const int position = static_cast<int>(state & StatePositionMask);
        const quint64 visited_mask = state >> StatePositionBits;
        if (visited_mask == full_mask && unresolved_terminals.contains(position)) {
            terminal_state_by_index.insert(position, state);
            unresolved_terminals.remove(position);
            if (unresolved_terminals.isEmpty()) {
                break;
            }
        }

        for (const int neighbor : neighbors.at(position)) {
            const quint64 next_state = stateKey(
                visited_mask | (quint64{1} << neighbor),
                neighbor);
            if (parent_by_state.contains(next_state)) {
                continue;
            }
            parent_by_state.insert(next_state, state);
            queue.enqueue(next_state);
        }
    }

    const auto routeForState = [&](quint64 terminal_state) {
        QStringList route;
        quint64 current_state = terminal_state;
        while (true) {
            const int position = static_cast<int>(current_state & StatePositionMask);
            route.prepend(cells.at(position));
            if (current_state == start_state) {
                break;
            }
            current_state = parent_by_state.value(current_state);
        }
        return route;
    };

    CoverageSearchResult result;
    result.expansions = expansions;
    result.proven_optimal = true;
    for (auto iterator = terminal_state_by_index.cbegin(); iterator != terminal_state_by_index.cend(); ++iterator) {
        const QStringList candidate_route = routeForState(iterator.value());
        if (isBetterMissionRoute(
                candidate_route,
                result.route,
                width,
                height,
                no_fly_cells,
                landing_profile,
                mission_timing)) {
            result.route = candidate_route;
        }
    }
    return result;
}

QStringList buildConnectedSweepRoute(
    const QStringList &ordered_cells,
    const QString &start_cell,
    const QString &terminal_cell,
    int width,
    int height,
    const QSet<QString> &no_fly_cells) {
    QStringList route{start_cell};
    const auto appendPathTo = [&](const QString &target) -> bool {
        const QStringList path = shortestPath(width, height, route.last(), target, no_fly_cells);
        if (path.isEmpty()) {
            return false;
        }
        route.append(path.mid(1));
        return true;
    };

    for (const QString &cell : ordered_cells) {
        if (!appendPathTo(cell)) {
            return {};
        }
    }
    if (!appendPathTo(terminal_cell)) {
        return {};
    }
    return route;
}

QStringList buildSweepCoverageSeed(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const QStringList &terminal_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    QStringList flyable_cells;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (!no_fly_cells.contains(cell)) {
                flyable_cells.append(cell);
            }
        }
    }

    QStringList best_route;
    for (const bool sweep_rows : {true, false}) {
        for (const int primary_direction : {1, -1}) {
            for (const int first_secondary_direction : {1, -1}) {
                QStringList ordered_cells = flyable_cells;
                std::sort(ordered_cells.begin(), ordered_cells.end(), [&](const QString &left, const QString &right) {
                    const QPoint left_point = decodeCell(left).value();
                    const QPoint right_point = decodeCell(right).value();
                    const int left_primary = sweep_rows ? left_point.y() : left_point.x();
                    const int right_primary = sweep_rows ? right_point.y() : right_point.x();
                    const int left_secondary = sweep_rows ? left_point.x() : left_point.y();
                    const int right_secondary = sweep_rows ? right_point.x() : right_point.y();
                    const int left_secondary_direction = ((left_primary % 2) == 0)
                        ? first_secondary_direction
                        : -first_secondary_direction;
                    const int right_secondary_direction = ((right_primary % 2) == 0)
                        ? first_secondary_direction
                        : -first_secondary_direction;
                    return std::make_tuple(
                               primary_direction * left_primary,
                               left_secondary_direction * left_secondary,
                               left)
                        < std::make_tuple(
                               primary_direction * right_primary,
                               right_secondary_direction * right_secondary,
                               right);
                });

                for (const QString &terminal_cell : terminal_cells) {
                    const QStringList candidate = buildConnectedSweepRoute(
                        ordered_cells,
                        start_cell,
                        terminal_cell,
                        width,
                        height,
                        no_fly_cells);
                    if (isBetterMissionRoute(
                            candidate,
                            best_route,
                            width,
                            height,
                            no_fly_cells,
                            landing_profile,
                            mission_timing)) {
                        best_route = candidate;
                    }
                }
            }
        }
    }
    return best_route;
}

struct TimeOptimalRouteResult {
    CoverageSearchResult search;
    QString failure_reason;
};

TimeOptimalRouteResult planTimeOptimalOpenRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    QStringList candidate_end_cells = terminalCellsForLanding(width, height, no_fly_cells, landing_profile).values();
    candidate_end_cells.sort();
    if (candidate_end_cells.isEmpty()) {
        return {{}, QStringLiteral("no landing-compatible terminal has a clear descent corridor")};
    }

    const int total_cells = (width * height) - no_fly_cells.size();
    if (total_cells <= 16) {
        CoverageSearchResult exact_search = planExactCoverageRoute(
            width,
            height,
            start_cell,
            no_fly_cells,
            candidate_end_cells,
            landing_profile,
            mission_timing);
        if (exact_search.route.isEmpty()) {
            return {exact_search, QStringLiteral("no coverage route reaches a landing-compatible terminal")};
        }
        return {exact_search, {}};
    }

    const QStringList incumbent_route = buildSweepCoverageSeed(
        width,
        height,
        start_cell,
        no_fly_cells,
        candidate_end_cells,
        landing_profile,
        mission_timing);
    CoverageSearchResult bounded_search = planBoundedCoverageRoute(
        width,
        height,
        start_cell,
        no_fly_cells,
        candidate_end_cells,
        incumbent_route,
        landing_profile,
        mission_timing,
        5000 * std::max(1, static_cast<int>(candidate_end_cells.size())));
    if (bounded_search.route.isEmpty()) {
        return {bounded_search, QStringLiteral("no coverage route reaches a landing-compatible terminal")};
    }
    return {bounded_search, {}};
}

}

QStringList planRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile,
    QString *error_message,
    MissionTiming mission_timing) {
    RouteRequest request;
    request.width = width;
    request.height = height;
    request.start_cell = start_cell;
    request.no_fly_cells = no_fly_cells;
    request.landing_profile = landing_profile;
    request.mission_timing = mission_timing;
    const RoutePlanResult result = planRoute(request);
    if (!result.ok && error_message != nullptr) {
        *error_message = result.failure_reason;
    } else if (error_message != nullptr) {
        error_message->clear();
    }
    return result.route;
}

RoutePlanResult planRoute(const RouteRequest &request) {
    RoutePlanResult result;
    QString error_message;

    if (!request.landing_profile.has_value()) {
        result.failure_reason = "landing_profile is required for H mission planning";
        return result;
    }
    if (request.no_fly_cells.contains(request.start_cell)) {
        result.failure_reason = "start cell cannot be inside the no-fly zone";
        return result;
    }
    const MissionTiming &timing = request.mission_timing;
    if (!std::isfinite(timing.cruise_speed_cm_per_s)
        || !std::isfinite(timing.ascent_speed_cm_per_s)
        || !std::isfinite(timing.descent_speed_cm_per_s)
        || !std::isfinite(timing.takeoff_fixed_time_s)
        || !std::isfinite(timing.landing_fixed_time_s)
        || !std::isfinite(timing.per_cell_dwell_time_s)
        || timing.cruise_speed_cm_per_s <= 0.0
        || timing.ascent_speed_cm_per_s <= 0.0
        || timing.descent_speed_cm_per_s <= 0.0
        || timing.takeoff_fixed_time_s < 0.0
        || timing.landing_fixed_time_s < 0.0
        || timing.per_cell_dwell_time_s < 0.0) {
        result.failure_reason = "mission timing must contain positive speeds and non-negative fixed or dwell times";
        return result;
    }
    const auto start_center = cellCodeCenterCm(request.start_cell, request.height);
    if (!start_center.has_value()
        || !descentCorridorIsClear(
            request.width,
            request.height,
            request.landing_profile->takeoff_anchor_cm,
            start_center.value(),
            request.no_fly_cells)) {
        result.failure_reason = "takeoff transit from the landing anchor to start_cell crosses a no-fly cell";
        return result;
    }
    const TimeOptimalRouteResult time_optimal_result = planTimeOptimalOpenRoute(
        request.width,
        request.height,
        request.start_cell,
        request.no_fly_cells,
        request.landing_profile.value(),
        request.mission_timing);
    result.route = time_optimal_result.search.route;
    result.search_expansions = time_optimal_result.search.expansions;
    result.search_optimality = time_optimal_result.search.proven_optimal
        ? SearchOptimality::ProvenOptimal
        : time_optimal_result.search.search_limit_reached
            ? SearchOptimality::SearchLimitReached
            : SearchOptimality::BestEffort;
    error_message = time_optimal_result.failure_reason;

    if (result.route.isEmpty()) {
        result.ok = false;
        result.failure_reason = error_message.isEmpty() ? QString("planner failed to produce a route") : error_message;
        result.search_optimality = SearchOptimality::NoFeasibleRoute;
        return result;
    }

    QSet<QString> covered_cells(result.route.begin(), result.route.end());
    for (const QString &blocked_cell : request.no_fly_cells) {
        covered_cells.remove(blocked_cell);
    }
    const int required_cells = (request.width * request.height) - request.no_fly_cells.size();
    result.coverage_rate = required_cells <= 0
        ? 0.0
        : static_cast<double>(covered_cells.size()) / static_cast<double>(required_cells);
    if (result.coverage_rate < 1.0) {
        result.failure_reason = QString("route covers %1% of required cells").arg(result.coverage_rate * 100.0, 0, 'f', 1);
        result.search_optimality = SearchOptimality::NoFeasibleRoute;
        return result;
    }
    result.estimated_mission_time_s = estimateMissionTimeSeconds(
        result.route,
        request.height,
        request.landing_profile,
        request.width,
        request.no_fly_cells,
        request.mission_timing);
    if (!std::isfinite(result.estimated_mission_time_s)) {
        result.failure_reason = "route does not have a valid timed landing approach";
        result.search_optimality = SearchOptimality::NoFeasibleRoute;
        return result;
    }
    result.ok = true;
    result.cost = result.estimated_mission_time_s;
    if (result.search_optimality == SearchOptimality::SearchLimitReached) {
        result.warnings.append("time-optimal search reached its expansion limit; reporting the best feasible route");
    } else if (result.search_optimality == SearchOptimality::BestEffort) {
        result.warnings.append("time-optimal search reports the best deterministic heuristic route; optimality is not proven");
    }
    return result;
}

} // namespace hcore
