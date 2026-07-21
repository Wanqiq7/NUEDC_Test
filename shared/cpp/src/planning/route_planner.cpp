#include "h_problem_core/planning/route_planner.h"

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"

#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <tuple>

namespace hcore {

namespace {

CellList gridNeighbors(const std::string &cell, int width, int height, const CellSet &no_fly_cells) {
    const auto decoded = decodeCell(cell);
    if (!decoded.has_value()) {
        return {};
    }
    const int x_index = decoded->x;
    const int y_index = decoded->y;
    const std::vector<GridPoint> candidates{
        {x_index + 1, y_index},
        {x_index - 1, y_index},
        {x_index, y_index + 1},
        {x_index, y_index - 1},
    };

    CellList neighbors;
    for (const GridPoint &candidate : candidates) {
        if (candidate.x < 0 || candidate.x >= width || candidate.y < 0 || candidate.y >= height) {
            continue;
        }
        const std::string next_cell = encodeCell(candidate.x, candidate.y);
        if (no_fly_cells.count(next_cell) == 0) {
            neighbors.push_back(next_cell);
        }
    }
    return neighbors;
}

CellList shortestPath(int width, int height, const std::string &start_cell, const std::string &end_cell, const CellSet &no_fly_cells) {
    std::queue<std::string> queue;
    queue.push(start_cell);
    std::map<std::string, std::string> parent;
    CellSet has_parent{start_cell};

    while (!queue.empty()) {
        const std::string cell = queue.front();
        queue.pop();
        if (cell == end_cell) {
            break;
        }
        for (const std::string &neighbor : gridNeighbors(cell, width, height, no_fly_cells)) {
            if (has_parent.count(neighbor) != 0) {
                continue;
            }
            has_parent.insert(neighbor);
            parent.emplace(neighbor, cell);
            queue.push(neighbor);
        }
    }

    if (has_parent.count(end_cell) == 0) {
        return {};
    }

    CellList path;
    std::string current = end_cell;
    while (current != start_cell) {
        path.insert(path.begin(), current);
        current = parent.at(current);
    }
    path.insert(path.begin(), start_cell);
    return path;
}

int countBits(std::uint64_t value) {
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
    const CellList &candidate,
    const CellList &current_best,
    int width,
    int height,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    if (current_best.empty()) {
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
    CellList route;
    int expansions = 0;
    bool proven_optimal = false;
    bool search_limit_reached = false;
    bool stopped_after_feasible_route = false;
};

CoverageSearchResult planBoundedCoverageRoute(
    int width,
    int height,
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const CellList &terminal_cells,
    const CellList &incumbent_route,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing,
    int expansion_limit) {
    struct Grid {
        std::vector<std::string> cells;
        std::map<std::string, int> index_by_cell;
        std::vector<std::vector<int>> neighbors;
        std::vector<std::vector<int>> distances;
        std::uint64_t full_mask = 0;
        int even_cells = 0;
        int odd_cells = 0;
    } grid;

    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const std::string cell = encodeCell(x_index, y_index);
            if (no_fly_cells.count(cell) != 0) {
                continue;
            }
            const int index = grid.cells.size();
            grid.cells.push_back(cell);
            grid.index_by_cell.emplace(cell, index);
            if ((x_index + y_index) % 2 == 0) {
                ++grid.even_cells;
            } else {
                ++grid.odd_cells;
            }
        }
    }

    const int total_cells = grid.cells.size();
    if (total_cells == 0 || total_cells > 63 || grid.index_by_cell.count(start_cell) == 0) {
        return {};
    }
    grid.full_mask = (std::uint64_t{1} << total_cells) - 1;

    grid.neighbors.resize(total_cells);
    for (int index = 0; index < total_cells; ++index) {
        const GridPoint point = decodeCell(grid.cells.at(index)).value();
        const std::vector<GridPoint> candidates{
            {point.x + 1, point.y},
            {point.x - 1, point.y},
            {point.x, point.y + 1},
            {point.x, point.y - 1},
        };
        for (const GridPoint &candidate : candidates) {
            const std::string neighbor = encodeCell(candidate.x, candidate.y);
            if (grid.index_by_cell.count(neighbor) != 0) {
                grid.neighbors[index].push_back(grid.index_by_cell.at(neighbor));
            }
        }
    }

    grid.distances.resize(total_cells);
    for (int source = 0; source < total_cells; ++source) {
        std::vector<int> &distances = grid.distances[source];
        distances.assign(total_cells, -1);
        std::queue<int> queue;
        distances[source] = 0;
        queue.push(source);
        while (!queue.empty()) {
            const int cell = queue.front();
            queue.pop();
            for (const int neighbor : grid.neighbors.at(cell)) {
                if (distances.at(neighbor) >= 0) {
                    continue;
                }
                distances[neighbor] = distances.at(cell) + 1;
                queue.push(neighbor);
            }
        }
    }

    const int start_index = grid.index_by_cell.at(start_cell);
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
    const GridPoint start_point = decodeCell(start_cell).value();
    const bool start_is_even = (start_point.x + start_point.y) % 2 == 0;
    CellList ordered_terminals = terminal_cells;
    std::sort(ordered_terminals.begin(), ordered_terminals.end(), [&](const std::string &left, const std::string &right) {
        const GridPoint left_point = decodeCell(left).value();
        const GridPoint right_point = decodeCell(right).value();
        const bool left_matches_start = ((left_point.x + left_point.y) % 2 == 0) == start_is_even;
        const bool right_matches_start = ((right_point.x + right_point.y) % 2 == 0) == start_is_even;
        return std::make_tuple(!left_matches_start, left) < std::make_tuple(!right_matches_start, right);
    });
    CellList best_route = incumbent_route;
    double best_time_s = best_route.empty()
        ? std::numeric_limits<double>::infinity()
        : estimateMissionTimeSeconds(best_route, height, landing_profile, width, no_fly_cells, mission_timing);
    int expansions = 0;
    bool search_limit_reached = false;
    bool stopped_after_feasible_route = false;
    const int terminal_count = std::max(1, static_cast<int>(ordered_terminals.size()));
    const int expansion_budget_per_terminal = std::max(1, expansion_limit / terminal_count);

    for (const std::string &terminal_cell : ordered_terminals) {
        if (expansions >= expansion_limit || grid.index_by_cell.count(terminal_cell) == 0) {
            break;
        }
        const int terminal_index = grid.index_by_cell.at(terminal_cell);
        if (grid.distances.at(start_index).at(terminal_index) < 0) {
            continue;
        }
        const GridPoint terminal_point = decodeCell(terminal_cell).value();
        const bool terminal_is_even = (terminal_point.x + terminal_point.y) % 2 == 0;
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
                std::uint64_t visited_mask = 0;
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
            std::vector<int> path{start_index};
            const std::uint64_t start_mask = std::uint64_t{1} << start_index;
            bool terminal_route_found = false;
            std::function<void(int, std::uint64_t, int)> dfs =
                [&](int position, std::uint64_t visited_mask, int repeats) {
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
                        CellList found_route;
                        for (const int index : path) {
                            found_route.push_back(grid.cells.at(index));
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

                    std::vector<Candidate> candidates;
                    for (const int neighbor : grid.neighbors.at(position)) {
                        const bool is_repeat = (visited_mask & (std::uint64_t{1} << neighbor)) != 0;
                        if (is_repeat && repeats >= repeat_cap) {
                            continue;
                        }
                        int onward_unvisited_degree = 0;
                        for (const int next_neighbor : grid.neighbors.at(neighbor)) {
                            if ((visited_mask & (std::uint64_t{1} << next_neighbor)) == 0) {
                                ++onward_unvisited_degree;
                            }
                        }
                        const GridPoint neighbor_point = decodeCell(grid.cells.at(neighbor)).value();
                        const int sweep_rank = (neighbor_point.y * width)
                            + (neighbor_point.y % 2 == 0
                                   ? neighbor_point.x
                                   : (width - 1 - neighbor_point.x));
                        candidates.push_back({
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
                        const std::uint64_t next_mask = visited_mask | (std::uint64_t{1} << candidate.index);
                        path.push_back(candidate.index);
                        dfs(candidate.index, next_mask, repeats + (candidate.is_repeat ? 1 : 0));
                        path.pop_back();
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
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const CellList &terminal_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    constexpr int ExactCoverageCellLimit = 16;
    constexpr int StatePositionBits = 6;
    constexpr std::uint64_t StatePositionMask = (std::uint64_t{1} << StatePositionBits) - 1;

    std::vector<std::string> cells;
    std::map<std::string, int> index_by_cell;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const std::string cell = encodeCell(x_index, y_index);
            if (no_fly_cells.count(cell) != 0) {
                continue;
            }
            index_by_cell.emplace(cell, cells.size());
            cells.push_back(cell);
        }
    }
    if (cells.empty()
        || cells.size() > ExactCoverageCellLimit
        || index_by_cell.count(start_cell) == 0) {
        return {};
    }

    std::vector<std::vector<int>> neighbors(cells.size());
    for (int index = 0; index < cells.size(); ++index) {
        const GridPoint point = decodeCell(cells.at(index)).value();
        for (const GridPoint &candidate : std::vector<GridPoint>{
                 {point.x + 1, point.y},
                 {point.x - 1, point.y},
                 {point.x, point.y + 1},
                 {point.x, point.y - 1},
             }) {
            const std::string neighbor = encodeCell(candidate.x, candidate.y);
            if (index_by_cell.count(neighbor) != 0) {
                neighbors[index].push_back(index_by_cell.at(neighbor));
            }
        }
    }

    std::set<int> unresolved_terminals;
    for (const std::string &terminal : terminal_cells) {
        if (index_by_cell.count(terminal) != 0) {
            unresolved_terminals.insert(index_by_cell.at(terminal));
        }
    }
    if (unresolved_terminals.empty()) {
        return {};
    }

    const auto stateKey = [](std::uint64_t visited_mask, int position) {
        return (visited_mask << StatePositionBits) | static_cast<std::uint64_t>(position);
    };
    const int start_index = index_by_cell.at(start_cell);
    const std::uint64_t start_mask = std::uint64_t{1} << start_index;
    const std::uint64_t full_mask = (std::uint64_t{1} << cells.size()) - 1;
    const std::uint64_t start_state = stateKey(start_mask, start_index);
    std::unordered_map<std::uint64_t, std::uint64_t> parent_by_state;
    parent_by_state.emplace(start_state, start_state);
    std::queue<std::uint64_t> queue;
    queue.push(start_state);
    std::map<int, std::uint64_t> terminal_state_by_index;
    int expansions = 0;

    while (!queue.empty()) {
        const std::uint64_t state = queue.front();
        queue.pop();
        ++expansions;
        const int position = static_cast<int>(state & StatePositionMask);
        const std::uint64_t visited_mask = state >> StatePositionBits;
        if (visited_mask == full_mask && unresolved_terminals.count(position) != 0) {
            terminal_state_by_index.emplace(position, state);
            unresolved_terminals.erase(position);
            if (unresolved_terminals.empty()) {
                break;
            }
        }

        for (const int neighbor : neighbors.at(position)) {
            const std::uint64_t next_state = stateKey(
                visited_mask | (std::uint64_t{1} << neighbor),
                neighbor);
            if (parent_by_state.count(next_state) != 0) {
                continue;
            }
            parent_by_state.emplace(next_state, state);
            queue.push(next_state);
        }
    }

    const auto routeForState = [&](std::uint64_t terminal_state) {
        CellList route;
        std::uint64_t current_state = terminal_state;
        while (true) {
            const int position = static_cast<int>(current_state & StatePositionMask);
            route.insert(route.begin(), cells.at(position));
            if (current_state == start_state) {
                break;
            }
            current_state = parent_by_state.at(current_state);
        }
        return route;
    };

    CoverageSearchResult result;
    result.expansions = expansions;
    result.proven_optimal = true;
    for (auto iterator = terminal_state_by_index.cbegin(); iterator != terminal_state_by_index.cend(); ++iterator) {
        const CellList candidate_route = routeForState(iterator->second);
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

CellList buildConnectedSweepRoute(
    const CellList &ordered_cells,
    const std::string &start_cell,
    const std::string &terminal_cell,
    int width,
    int height,
    const CellSet &no_fly_cells) {
    CellList route{start_cell};
    const auto appendPathTo = [&](const std::string &target) -> bool {
        const CellList path = shortestPath(width, height, route.back(), target, no_fly_cells);
        if (path.empty()) {
            return false;
        }
        route.insert(route.end(), std::next(path.begin()), path.end());
        return true;
    };

    for (const std::string &cell : ordered_cells) {
        if (!appendPathTo(cell)) {
            return {};
        }
    }
    if (!appendPathTo(terminal_cell)) {
        return {};
    }
    return route;
}

CellList buildSweepCoverageSeed(
    int width,
    int height,
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const CellList &terminal_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    CellList flyable_cells;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const std::string cell = encodeCell(x_index, y_index);
            if (no_fly_cells.count(cell) == 0) {
                flyable_cells.push_back(cell);
            }
        }
    }

    CellList best_route;
    for (const bool sweep_rows : {true, false}) {
        for (const int primary_direction : {1, -1}) {
            for (const int first_secondary_direction : {1, -1}) {
                CellList ordered_cells = flyable_cells;
                std::sort(ordered_cells.begin(), ordered_cells.end(), [&](const std::string &left, const std::string &right) {
                    const GridPoint left_point = decodeCell(left).value();
                    const GridPoint right_point = decodeCell(right).value();
                    const int left_primary = sweep_rows ? left_point.y : left_point.x;
                    const int right_primary = sweep_rows ? right_point.y : right_point.x;
                    const int left_secondary = sweep_rows ? left_point.x : left_point.y;
                    const int right_secondary = sweep_rows ? right_point.x : right_point.y;
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

                for (const std::string &terminal_cell : terminal_cells) {
                    const CellList candidate = buildConnectedSweepRoute(
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
    std::string failure_reason;
};

TimeOptimalRouteResult planTimeOptimalOpenRoute(
    int width,
    int height,
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile,
    const MissionTiming &mission_timing) {
    const CellSet terminal_set = terminalCellsForLanding(width, height, no_fly_cells, landing_profile);
    CellList candidate_end_cells(terminal_set.begin(), terminal_set.end());
    if (candidate_end_cells.empty()) {
        return {{}, "no landing-compatible terminal has a clear descent corridor"};
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
        if (exact_search.route.empty()) {
            return {exact_search, "no coverage route reaches a landing-compatible terminal"};
        }
        return {exact_search, {}};
    }

    const CellList incumbent_route = buildSweepCoverageSeed(
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
    if (bounded_search.route.empty()) {
        return {bounded_search, "no coverage route reaches a landing-compatible terminal"};
    }
    return {bounded_search, {}};
}

}

CellList planRoute(
    int width,
    int height,
    const std::string &start_cell,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile,
    std::string *error_message,
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
    std::string error_message;

    if (!request.landing_profile.has_value()) {
        result.failure_reason = "landing_profile is required for H mission planning";
        return result;
    }
    if (request.no_fly_cells.count(request.start_cell) != 0) {
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

    if (result.route.empty()) {
        result.ok = false;
        result.failure_reason = error_message.empty() ? std::string("planner failed to produce a route") : error_message;
        result.search_optimality = SearchOptimality::NoFeasibleRoute;
        return result;
    }

    CellSet covered_cells(result.route.begin(), result.route.end());
    for (const std::string &blocked_cell : request.no_fly_cells) {
        covered_cells.erase(blocked_cell);
    }
    const int required_cells = (request.width * request.height) - request.no_fly_cells.size();
    result.coverage_rate = required_cells <= 0
        ? 0.0
        : static_cast<double>(covered_cells.size()) / static_cast<double>(required_cells);
    if (result.coverage_rate < 1.0) {
        std::ostringstream message;
        message << "route covers " << std::fixed << std::setprecision(1)
                << result.coverage_rate * 100.0 << "% of required cells";
        result.failure_reason = message.str();
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
        result.warnings.push_back("time-optimal search reached its expansion limit; reporting the best feasible route");
    } else if (result.search_optimality == SearchOptimality::BestEffort) {
        result.warnings.push_back("time-optimal search reports the best deterministic heuristic route; optimality is not proven");
    }
    return result;
}

} // namespace hcore
