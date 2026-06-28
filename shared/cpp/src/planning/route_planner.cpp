#include "h_problem_core/planning/route_planner.h"

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"

#include <QElapsedTimer>
#include <QQueue>

#include <algorithm>
#include <queue>

namespace hcore {

namespace {

bool containsCell(const QStringList &cells, const QString &cell) {
    return cells.contains(cell);
}

int reachableComponentSize(const QString &start_cell, int width, int height, const QSet<QString> &blocked_cells) {
    if (blocked_cells.contains(start_cell)) {
        return 0;
    }

    QStringList stack{start_cell};
    QSet<QString> seen{start_cell};
    while (!stack.isEmpty()) {
        const QString cell = stack.takeLast();
        const auto decoded = decodeCell(cell);
        if (!decoded.has_value()) {
            continue;
        }
        const QPoint point = decoded.value();
        const QVector<QPoint> candidates{
            {point.x() + 1, point.y()},
            {point.x() - 1, point.y()},
            {point.x(), point.y() + 1},
            {point.x(), point.y() - 1},
        };
        for (const QPoint &next_point : candidates) {
            if (next_point.x() < 0 || next_point.x() >= width || next_point.y() < 0 || next_point.y() >= height) {
                continue;
            }
            const QString next_cell = encodeCell(next_point.x(), next_point.y());
            if (blocked_cells.contains(next_cell) || seen.contains(next_cell)) {
                continue;
            }
            seen.insert(next_cell);
            stack.append(next_cell);
        }
    }
    return seen.size();
}

QStringList orderedNeighbors(
    const QString &cell,
    int width,
    int height,
    const QSet<QString> &visited,
    const QSet<QString> &no_fly_cells) {
    const auto decoded = decodeCell(cell);
    if (!decoded.has_value()) {
        return {};
    }
    const int x_index = decoded->x();
    const int y_index = decoded->y();
    QVector<QPoint> candidates;
    if (y_index % 2 == 0) {
        candidates = {{x_index + 1, y_index}, {x_index, y_index + 1}, {x_index - 1, y_index}, {x_index, y_index - 1}};
    } else {
        candidates = {{x_index - 1, y_index}, {x_index, y_index + 1}, {x_index + 1, y_index}, {x_index, y_index - 1}};
    }

    struct RankedCell {
        int branch_size = 0;
        int y = 0;
        int x = 0;
        QString cell;
    };
    QVector<RankedCell> ranked_candidates;
    for (const QPoint &candidate : candidates) {
        if (candidate.x() < 0 || candidate.x() >= width || candidate.y() < 0 || candidate.y() >= height) {
            continue;
        }
        const QString next_cell = encodeCell(candidate.x(), candidate.y());
        if (no_fly_cells.contains(next_cell) || visited.contains(next_cell)) {
            continue;
        }
        QSet<QString> blocked = no_fly_cells;
        blocked.unite(visited);
        blocked.insert(cell);
        ranked_candidates.append({reachableComponentSize(next_cell, width, height, blocked), candidate.y(), candidate.x(), next_cell});
    }

    std::sort(ranked_candidates.begin(), ranked_candidates.end(), [](const RankedCell &left, const RankedCell &right) {
        return std::tie(left.branch_size, left.y, left.x, left.cell) < std::tie(right.branch_size, right.y, right.x, right.cell);
    });

    QStringList result;
    for (const RankedCell &ranked : ranked_candidates) {
        result.append(ranked.cell);
    }
    return result;
}

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

bool isRemainingGraphConnected(const QStringList &nodes, const QMap<QString, QStringList> &adjacency, const QSet<QString> &visited) {
    QStringList remaining_nodes;
    for (const QString &node : nodes) {
        if (!visited.contains(node)) {
            remaining_nodes.append(node);
        }
    }
    if (remaining_nodes.isEmpty()) {
        return true;
    }

    const QSet<QString> remaining_set(remaining_nodes.begin(), remaining_nodes.end());
    QStringList stack{remaining_nodes.first()};
    QSet<QString> seen{remaining_nodes.first()};
    while (!stack.isEmpty()) {
        const QString node = stack.takeLast();
        for (const QString &neighbor : adjacency.value(node)) {
            if (remaining_set.contains(neighbor) && !seen.contains(neighbor)) {
                seen.insert(neighbor);
                stack.append(neighbor);
            }
        }
    }
    return seen.size() == remaining_nodes.size();
}

QStringList attemptExactHamiltonPath(int width, int height, const QString &start_cell, const QSet<QString> &no_fly_cells, int time_budget_ms) {
    QStringList nodes;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (!no_fly_cells.contains(cell)) {
                nodes.append(cell);
            }
        }
    }

    QMap<QString, QStringList> adjacency;
    for (const QString &node : nodes) {
        adjacency.insert(node, orderedNeighbors(node, width, height, {}, no_fly_cells));
    }

    QElapsedTimer timer;
    timer.start();
    QSet<QString> visited{start_cell};
    QStringList path{start_cell};

    std::function<QStringList(const QString &)> dfs = [&](const QString &cell) -> QStringList {
        if (timer.elapsed() > time_budget_ms) {
            return {};
        }
        if (path.size() == nodes.size()) {
            return path;
        }
        if (!isRemainingGraphConnected(nodes, adjacency, visited)) {
            return {};
        }

        QStringList candidates;
        for (const QString &neighbor : adjacency.value(cell)) {
            if (!visited.contains(neighbor)) {
                candidates.append(neighbor);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [&](const QString &left, const QString &right) {
            const QStringList left_adjacency = adjacency.value(left);
            const QStringList right_adjacency = adjacency.value(right);
            const int left_degree = std::count_if(left_adjacency.begin(), left_adjacency.end(), [&](const QString &neighbor) {
                return !visited.contains(neighbor);
            });
            const int right_degree = std::count_if(right_adjacency.begin(), right_adjacency.end(), [&](const QString &neighbor) {
                return !visited.contains(neighbor);
            });
            const auto left_point = decodeCell(left).value();
            const auto right_point = decodeCell(right).value();
            return std::make_tuple(left_degree, left_point.y(), left_point.x(), left) < std::make_tuple(right_degree, right_point.y(), right_point.x(), right);
        });

        for (const QString &neighbor : candidates) {
            visited.insert(neighbor);
            path.append(neighbor);
            const QStringList exact_path = dfs(neighbor);
            if (!exact_path.isEmpty()) {
                return exact_path;
            }
            path.removeLast();
            visited.remove(neighbor);
        }
        return {};
    };

    return dfs(start_cell);
}

QStringList attemptExactHamiltonCycle(int width, int height, const QString &start_cell, const QSet<QString> &no_fly_cells, int time_budget_ms) {
    QStringList nodes;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (!no_fly_cells.contains(cell)) {
                nodes.append(cell);
            }
        }
    }

    QMap<QString, QStringList> adjacency;
    for (const QString &node : nodes) {
        adjacency.insert(node, orderedNeighbors(node, width, height, {}, no_fly_cells));
    }

    QElapsedTimer timer;
    timer.start();
    QSet<QString> visited{start_cell};
    QStringList path{start_cell};

    std::function<QStringList(const QString &)> dfs = [&](const QString &cell) -> QStringList {
        if (timer.elapsed() > time_budget_ms) {
            return {};
        }
        if (path.size() == nodes.size()) {
            if (adjacency.value(cell).contains(start_cell)) {
                QStringList cycle = path;
                cycle.append(start_cell);
                return cycle;
            }
            return {};
        }
        if (!isRemainingGraphConnected(nodes, adjacency, visited)) {
            return {};
        }

        QStringList candidates;
        for (const QString &neighbor : adjacency.value(cell)) {
            if (!visited.contains(neighbor)) {
                candidates.append(neighbor);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [&](const QString &left, const QString &right) {
            const QStringList left_adjacency = adjacency.value(left);
            const QStringList right_adjacency = adjacency.value(right);
            const int left_degree = std::count_if(left_adjacency.begin(), left_adjacency.end(), [&](const QString &neighbor) {
                return !visited.contains(neighbor);
            });
            const int right_degree = std::count_if(right_adjacency.begin(), right_adjacency.end(), [&](const QString &neighbor) {
                return !visited.contains(neighbor);
            });
            const auto left_point = decodeCell(left).value();
            const auto right_point = decodeCell(right).value();
            return std::make_tuple(left_degree, left_point.y(), left_point.x(), left) < std::make_tuple(right_degree, right_point.y(), right_point.x(), right);
        });

        for (const QString &neighbor : candidates) {
            visited.insert(neighbor);
            path.append(neighbor);
            const QStringList exact_cycle = dfs(neighbor);
            if (!exact_cycle.isEmpty()) {
                return exact_cycle;
            }
            path.removeLast();
            visited.remove(neighbor);
        }
        return {};
    };

    return dfs(start_cell);
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
        for (const QString &neighbor : orderedNeighbors(cell, width, height, {}, no_fly_cells)) {
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

QStringList exactCompletionToEnd(
    int width,
    int height,
    const QString &current_cell,
    const QStringList &required_cells,
    const QString &end_cell,
    const QSet<QString> &no_fly_cells) {
    QMap<QString, int> terminal_index;
    for (int index = 0; index < required_cells.size(); ++index) {
        terminal_index.insert(required_cells.at(index), index);
    }
    const quint64 full_mask = required_cells.isEmpty() ? 0 : ((quint64{1} << required_cells.size()) - 1);
    quint64 start_mask = 0;
    if (terminal_index.contains(current_cell)) {
        start_mask |= quint64{1} << terminal_index.value(current_cell);
    }

    QMap<QString, int> distance_to_end;
    distance_to_end.insert(end_cell, 0);
    QQueue<QString> queue;
    queue.enqueue(end_cell);
    while (!queue.isEmpty()) {
        const QString cell = queue.dequeue();
        for (const QString &neighbor : gridNeighbors(cell, width, height, no_fly_cells)) {
            if (distance_to_end.contains(neighbor)) {
                continue;
            }
            distance_to_end.insert(neighbor, distance_to_end.value(cell) + 1);
            queue.enqueue(neighbor);
        }
    }
    if (!distance_to_end.contains(current_cell)) {
        return {};
    }

    struct State {
        QString cell;
        quint64 mask = 0;

        bool operator<(const State &other) const {
            return std::tie(cell, mask) < std::tie(other.cell, other.mask);
        }
    };
    struct QueueItem {
        int priority = 0;
        int cost = 0;
        QString cell;
        quint64 mask = 0;
    };
    struct QueueCompare {
        bool operator()(const QueueItem &left, const QueueItem &right) const {
            return std::tie(left.priority, left.cost, left.cell, left.mask) > std::tie(right.priority, right.cost, right.cell, right.mask);
        }
    };

    const State start_state{current_cell, start_mask};
    QMap<State, int> best_cost;
    QMap<State, std::optional<State>> parent;
    best_cost.insert(start_state, 0);
    parent.insert(start_state, std::nullopt);

    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueCompare> priority_queue;
    priority_queue.push({distance_to_end.value(current_cell), 0, current_cell, start_mask});

    while (!priority_queue.empty()) {
        const QueueItem item = priority_queue.top();
        priority_queue.pop();
        const State state{item.cell, item.mask};
        if (item.cost != best_cost.value(state, 1000000000)) {
            continue;
        }
        if (item.cell == end_cell && item.mask == full_mask) {
            QStringList completion;
            std::optional<State> current = state;
            while (current.has_value()) {
                completion.prepend(current->cell);
                current = parent.value(current.value());
            }
            return completion;
        }

        for (const QString &neighbor : gridNeighbors(item.cell, width, height, no_fly_cells)) {
            if (!distance_to_end.contains(neighbor)) {
                continue;
            }
            quint64 next_mask = item.mask;
            if (terminal_index.contains(neighbor)) {
                next_mask |= quint64{1} << terminal_index.value(neighbor);
            }
            const State next_state{neighbor, next_mask};
            const int next_cost = item.cost + 1;
            if (next_cost >= best_cost.value(next_state, 1000000000)) {
                continue;
            }
            best_cost.insert(next_state, next_cost);
            parent.insert(next_state, state);
            priority_queue.push({next_cost + distance_to_end.value(neighbor), next_cost, neighbor, next_mask});
        }
    }

    return {};
}

QStringList optimizeRouteSuffixToEnd(
    const QStringList &route,
    int width,
    int height,
    const QString &end_cell,
    const QSet<QString> &no_fly_cells,
    int max_remaining_cells = 10,
    int max_cut_candidates = 3) {
    QSet<QString> all_cells;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = encodeCell(x_index, y_index);
            if (!no_fly_cells.contains(cell)) {
                all_cells.insert(cell);
            }
        }
    }

    QStringList best_route = route.last() == end_cell ? route : QStringList{};
    QSet<QString> seen_cells;
    int tested_candidates = 0;

    for (int cut_index = 0; cut_index < route.size(); ++cut_index) {
        const QString cell = route.at(cut_index);
        seen_cells.insert(cell);
        QStringList remaining_cells = (all_cells - seen_cells).values();
        remaining_cells.sort();
        if (remaining_cells.size() > max_remaining_cells) {
            continue;
        }

        const QStringList suffix = exactCompletionToEnd(width, height, cell, remaining_cells, end_cell, no_fly_cells);
        ++tested_candidates;
        if (!suffix.isEmpty()) {
            QStringList candidate_route = route.mid(0, cut_index);
            candidate_route.append(suffix);
            if (best_route.isEmpty() || candidate_route.size() < best_route.size()) {
                best_route = candidate_route;
            }
        }

        if (tested_candidates >= max_cut_candidates) {
            break;
        }
    }

    return best_route;
}

QStringList planLegacyRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    std::optional<QString> end_cell,
    bool require_cycle,
    QString *error_message) {
    if (no_fly_cells.contains(start_cell)) {
        if (error_message != nullptr) {
            *error_message = "start cell cannot be inside the no-fly zone";
        }
        return {};
    }
    if (end_cell.has_value() && no_fly_cells.contains(end_cell.value())) {
        if (error_message != nullptr) {
            *error_message = "end cell cannot be inside the no-fly zone";
        }
        return {};
    }

    const int total_cells = (width * height) - no_fly_cells.size();
    if (require_cycle) {
        const QString cycle_target = end_cell.value_or(start_cell);
        if (cycle_target != start_cell) {
            if (error_message != nullptr) {
                *error_message = "closed tour currently requires end_cell to match start_cell";
            }
            return {};
        }
        if (total_cells <= 25) {
            const QStringList exact_cycle = attemptExactHamiltonCycle(width, height, start_cell, no_fly_cells, 1000);
            if (!exact_cycle.isEmpty()) {
                return exact_cycle;
            }
        }
    }

    if (total_cells <= 25) {
        const QStringList exact_path = attemptExactHamiltonPath(width, height, start_cell, no_fly_cells, 1000);
        if (!exact_path.isEmpty() && (!end_cell.has_value() || exact_path.last() == end_cell.value())) {
            return exact_path;
        }
    }

    QSet<QString> visited{start_cell};
    QMap<QString, QStringList> spanning_tree;
    QMap<QString, QString> parent;
    QSet<QString> has_parent{start_cell};
    QMap<QString, int> depth;
    depth.insert(start_cell, 0);

    std::function<void(const QString &)> build_spanning_tree = [&](const QString &cell) {
        spanning_tree.insert(cell, {});
        const QStringList neighbors = orderedNeighbors(cell, width, height, visited, no_fly_cells);
        for (const QString &neighbor : neighbors) {
            if (visited.contains(neighbor)) {
                continue;
            }
            visited.insert(neighbor);
            parent.insert(neighbor, cell);
            has_parent.insert(neighbor);
            depth.insert(neighbor, depth.value(cell) + 1);
            auto children = spanning_tree.value(cell);
            children.append(neighbor);
            spanning_tree.insert(cell, children);
            build_spanning_tree(neighbor);
        }
    };
    build_spanning_tree(start_cell);

    if (visited.size() != total_cells) {
        if (error_message != nullptr) {
            *error_message = "planner failed to cover every reachable non-forbidden cell";
        }
        return {};
    }

    const std::optional<QString> target_cell = require_cycle ? std::optional<QString>(start_cell) : end_cell;
    QStringList leaf_cells;
    for (const QString &cell : visited) {
        if (spanning_tree.value(cell).isEmpty()) {
            leaf_cells.append(cell);
        }
    }
    if (leaf_cells.isEmpty()) {
        leaf_cells.append(start_cell);
    }

    QMap<QString, int> distance_cache;
    auto shortest_distance = [&](const QString &from_cell, const std::optional<QString> &to_cell) -> int {
        if (!to_cell.has_value()) {
            return 0;
        }
        const QString cache_key = from_cell + "->" + to_cell.value();
        if (distance_cache.contains(cache_key)) {
            return distance_cache.value(cache_key);
        }
        const QStringList path = shortestPath(width, height, from_cell, to_cell.value(), no_fly_cells);
        const int distance = path.isEmpty() ? 1000000 : path.size() - 1;
        distance_cache.insert(cache_key, distance);
        return distance;
    };

    QString terminal_leaf = leaf_cells.first();
    auto terminal_rank = [&](const QString &cell) {
        const auto point = decodeCell(cell).value();
        return std::make_tuple(depth[cell] - shortest_distance(cell, target_cell), depth[cell], point.y(), point.x(), cell);
    };
    for (const QString &cell : leaf_cells) {
        if (terminal_rank(cell) > terminal_rank(terminal_leaf)) {
            terminal_leaf = cell;
        }
    }

    QMap<QString, QString> terminal_child_by_parent;
    QString current = terminal_leaf;
    while (has_parent.contains(current) && parent.contains(current)) {
        const QString current_parent = parent.value(current);
        terminal_child_by_parent.insert(current_parent, current);
        current = current_parent;
    }

    QStringList route{start_cell};
    std::function<void(const QString &)> emit_route = [&](const QString &cell) {
        const QStringList children = spanning_tree.value(cell);
        const QString terminal_child = terminal_child_by_parent.value(cell);
        for (const QString &child : children) {
            if (!terminal_child.isEmpty() && child == terminal_child) {
                continue;
            }
            route.append(child);
            emit_route(child);
            route.append(cell);
        }
        if (!terminal_child.isEmpty()) {
            route.append(terminal_child);
            emit_route(terminal_child);
        }
    };
    emit_route(start_cell);

    if (require_cycle) {
        const QStringList optimized_cycle = optimizeRouteSuffixToEnd(route, width, height, start_cell, no_fly_cells);
        if (!optimized_cycle.isEmpty()) {
            return optimized_cycle;
        }
        const QStringList return_path = shortestPath(width, height, route.last(), start_cell, no_fly_cells);
        if (return_path.size() > 1) {
            route.append(return_path.mid(1));
        }
        return route;
    }

    if (end_cell.has_value() && route.last() != end_cell.value()) {
        const QStringList optimized_route = optimizeRouteSuffixToEnd(route, width, height, end_cell.value(), no_fly_cells);
        if (!optimized_route.isEmpty()) {
            return optimized_route;
        }
        const QStringList tail_path = shortestPath(width, height, route.last(), end_cell.value(), no_fly_cells);
        if (tail_path.size() > 1) {
            route.append(tail_path.mid(1));
        }
    }

    return route;
}

QStringList planTimeOptimalOpenRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile,
    QString *error_message) {
    QStringList candidate_end_cells = terminalCellsForLanding(width, height, no_fly_cells, landing_profile).values();
    candidate_end_cells.sort();
    if (candidate_end_cells.isEmpty()) {
        return planLegacyRoute(width, height, start_cell, no_fly_cells, {}, false, error_message);
    }

    QVector<QStringList> candidate_routes;
    for (const QString &end_cell : candidate_end_cells) {
        QString candidate_error;
        const QStringList candidate_route = planLegacyRoute(width, height, start_cell, no_fly_cells, end_cell, false, &candidate_error);
        if (!candidate_route.isEmpty() && candidate_route.last() == end_cell) {
            candidate_routes.append(candidate_route);
        }
    }

    if (candidate_routes.isEmpty()) {
        return planLegacyRoute(width, height, start_cell, no_fly_cells, {}, false, error_message);
    }

    return *std::min_element(candidate_routes.begin(), candidate_routes.end(), [&](const QStringList &left, const QStringList &right) {
        return estimateRouteCost(left, height, 18.0, 6.0, landing_profile, width, no_fly_cells)
            < estimateRouteCost(right, height, 18.0, 6.0, landing_profile, width, no_fly_cells);
    });
}

}

QStringList planRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    std::optional<QString> end_cell,
    bool require_cycle,
    MissionMode mission_mode,
    std::optional<LandingProfile> landing_profile,
    QString *error_message) {
    if (mission_mode == MissionMode::TimeOptimalOpen) {
        if (!landing_profile.has_value()) {
            if (error_message != nullptr) {
                *error_message = "landing_profile is required when mission_mode is time_optimal_open";
            }
            return {};
        }
        return planTimeOptimalOpenRoute(width, height, start_cell, no_fly_cells, landing_profile.value(), error_message);
    }

    return planLegacyRoute(width, height, start_cell, no_fly_cells, end_cell, require_cycle, error_message);
}

RoutePlanResult planRoute(const RouteRequest &request) {
    RoutePlanResult result;
    QString error_message;
    result.route = planRoute(
        request.width,
        request.height,
        request.start_cell,
        request.no_fly_cells,
        request.end_cell,
        request.require_cycle,
        request.mission_mode,
        request.landing_profile,
        &error_message);

    if (result.route.isEmpty()) {
        result.ok = false;
        result.failure_reason = error_message.isEmpty() ? QString("planner failed to produce a route") : error_message;
        return result;
    }

    QSet<QString> covered_cells(result.route.begin(), result.route.end());
    for (const QString &blocked_cell : request.no_fly_cells) {
        covered_cells.remove(blocked_cell);
    }
    const int required_cells = (request.width * request.height) - request.no_fly_cells.size();
    result.ok = true;
    result.cost = estimateRouteCost(
        result.route,
        request.height,
        18.0,
        6.0,
        request.landing_profile,
        request.width,
        request.no_fly_cells);
    result.coverage_rate = required_cells <= 0
        ? 0.0
        : static_cast<double>(covered_cells.size()) / static_cast<double>(required_cells);
    if (result.coverage_rate < 1.0) {
        result.warnings.append(QString("route covers %1% of required cells").arg(result.coverage_rate * 100.0, 0, 'f', 1));
    }
    return result;
}

PlanningResult planRouteWithDetails(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &no_fly_cells,
    std::optional<QString> end_cell,
    bool require_cycle,
    MissionMode mission_mode,
    std::optional<LandingProfile> landing_profile) {
    return planRoute(RouteRequest{
        width,
        height,
        start_cell,
        no_fly_cells,
        end_cell,
        require_cycle,
        mission_mode,
        landing_profile,
    });
}

QStringList exactCompletionToEndForTesting(
    int width,
    int height,
    const QString &current_cell,
    const QStringList &required_cells,
    const QString &end_cell,
    const QSet<QString> &no_fly_cells) {
    return exactCompletionToEnd(width, height, current_cell, required_cells, end_cell, no_fly_cells);
}

} // namespace hcore
