#include <gtest/gtest.h>

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"
#include "h_problem_core/planning/route_planner.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <unordered_map>

namespace {

hcore::GridPoint toPoint(const std::string &cell) {
    const auto point = hcore::decodeCell(cell);
    EXPECT_TRUE(point.has_value());
    return point.value_or(hcore::GridPoint{});
}

void verifyAdjacentRoute(const hcore::CellList &route) {
    for (std::size_t index = 1; index < route.size(); ++index) {
        const auto previous = toPoint(route[index - 1]);
        const auto current = toPoint(route[index]);
        EXPECT_EQ(std::abs(previous.x - current.x) + std::abs(previous.y - current.y), 1);
    }
}

hcore::LandingProfile landingProfileForTerminal(const std::string &terminal_cell, int height) {
    const auto center = hcore::cellCodeCenterCm(terminal_cell, height);
    EXPECT_TRUE(center.has_value());
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {center->x_cm + 100.0, center->y_cm + 100.0};
    profile.cruise_height_cm = 140.0;
    profile.descent_angle_deg = 45.0;
    profile.touchdown_radius_cm = 18.0;
    profile.preferred_heading_deg = 45.0;
    profile.heading_tolerance_deg = 5.0;
    return profile;
}

hcore::LandingProfile sampleLandingProfile() {
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {450.0, 350.0};
    profile.cruise_height_cm = 120.0;
    profile.descent_angle_deg = 45.0;
    profile.touchdown_radius_cm = 18.0;
    return profile;
}

hcore::CellSet independentlyEnumerateTerminals(int width, int height,
        const hcore::CellSet &blocked, const hcore::LandingProfile &profile) {
    hcore::CellSet result;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto cell = hcore::encodeCell(x, y);
            if (blocked.count(cell) == 0
                && hcore::landingApproachForTerminal(width, height, cell, blocked, profile)) {
                result.insert(cell);
            }
        }
    }
    return result;
}

hcore::CellList exactTimeOptimalRoute(int width, int height, const std::string &start,
        const hcore::CellSet &blocked, const hcore::CellSet &terminals,
        const hcore::LandingProfile &profile, const hcore::MissionTiming &timing) {
    constexpr int PositionBits = 6;
    constexpr std::uint64_t PositionMask = (std::uint64_t{1} << PositionBits) - 1;
    hcore::CellList cells;
    std::map<std::string, int> index_by_cell;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto cell = hcore::encodeCell(x, y);
        if (blocked.count(cell) == 0) { index_by_cell[cell] = cells.size(); cells.push_back(cell); }
    }
    if (cells.size() > 16 || index_by_cell.count(start) == 0) return {};
    std::vector<std::vector<int>> neighbors(cells.size());
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto point = toPoint(cells[index]);
        for (const auto candidate : {hcore::GridPoint{point.x + 1, point.y},
                {point.x - 1, point.y}, {point.x, point.y + 1}, {point.x, point.y - 1}}) {
            const auto found = index_by_cell.find(hcore::encodeCell(candidate.x, candidate.y));
            if (found != index_by_cell.end()) neighbors[index].push_back(found->second);
        }
    }
    const int start_index = index_by_cell.at(start);
    const auto key = [](std::uint64_t mask, int position) { return (mask << PositionBits) | position; };
    const std::uint64_t full_mask = (std::uint64_t{1} << cells.size()) - 1;
    const std::uint64_t start_state = key(std::uint64_t{1} << start_index, start_index);
    std::set<int> unresolved;
    for (const auto &terminal : terminals) if (index_by_cell.count(terminal)) unresolved.insert(index_by_cell.at(terminal));
    std::unordered_map<std::uint64_t, std::uint64_t> parents{{start_state, start_state}};
    std::queue<std::uint64_t> queue;
    queue.push(start_state);
    std::map<int, std::uint64_t> terminal_states;
    while (!queue.empty() && !unresolved.empty()) {
        const auto state = queue.front(); queue.pop();
        const int position = state & PositionMask;
        const auto mask = state >> PositionBits;
        if (mask == full_mask && unresolved.count(position)) {
            terminal_states[position] = state; unresolved.erase(position);
        }
        for (int neighbor : neighbors[position]) {
            const auto next = key(mask | (std::uint64_t{1} << neighbor), neighbor);
            if (parents.emplace(next, state).second) queue.push(next);
        }
    }
    hcore::CellList best;
    for (const auto &[position, terminal_state] : terminal_states) {
        (void) position;
        hcore::CellList candidate;
        auto state = terminal_state;
        while (true) {
            candidate.insert(candidate.begin(), cells[state & PositionMask]);
            if (state == start_state) break;
            state = parents.at(state);
        }
        const double candidate_time = hcore::estimateMissionTimeSeconds(candidate, height, profile, width, blocked, timing);
        const double best_time = best.empty() ? std::numeric_limits<double>::infinity()
            : hcore::estimateMissionTimeSeconds(best, height, profile, width, blocked, timing);
        if (candidate_time < best_time - 1e-9
            || (std::abs(candidate_time - best_time) <= 1e-9 && candidate < best)) best = candidate;
    }
    return best;
}

} // namespace

TEST(HRoutePlanner, PrefersLandingCompatibleTerminalRegionAndKeepsRepeat) {
    const auto profile = sampleLandingProfile();
    const hcore::CellSet blocked{"A4B3", "A5B3", "A6B3"};
    std::string error;
    const auto route = hcore::planRoute(9, 7, "A9B1", blocked, profile, &error);
    ASSERT_FALSE(route.empty()) << error;
    EXPECT_EQ(route.front(), "A9B1");
    EXPECT_TRUE(hcore::terminalCellsForLanding(9, 7, blocked, profile).count(route.back()));
    EXPECT_EQ(hcore::CellSet(route.begin(), route.end()).size(), 60U);
    EXPECT_EQ(route.size(), 61U);
    verifyAdjacentRoute(route);
}

TEST(HRoutePlanner, RejectsLandingCorridorAcrossNoFlyCell) {
    const hcore::CellSet blocked{"A8B2"};
    EXPECT_EQ(hcore::terminalCellsForLanding(9, 7, blocked, sampleLandingProfile()).count("A7B3"), 0U);
    const auto start = hcore::cellCodeCenterCm("A7B3", 7);
    ASSERT_TRUE(start.has_value());
    EXPECT_FALSE(hcore::descentCorridorIsClear(9, 7, *start, {450.0, 350.0}, blocked));
    EXPECT_TRUE(hcore::descentCorridorIsClear(9, 7, *start, {450.0, 350.0}, {}));
}

TEST(HRoutePlanner, TouchdownRadiusMakesShortApproachFeasible) {
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {115.0, 115.0};
    profile.cruise_height_cm = 120.0;
    profile.descent_angle_deg = 45.0;
    profile.preferred_heading_deg = 45.0;
    profile.heading_tolerance_deg = 0.0;
    EXPECT_FALSE(hcore::landingApproachForTerminal(5, 5, "A1B5", {}, profile));
    profile.touchdown_radius_cm = 10.0;
    const auto approach = hcore::landingApproachForTerminal(5, 5, "A1B5", {}, profile);
    ASSERT_TRUE(approach.has_value());
    EXPECT_GE(approach->horizontal_run_cm, hcore::computeDescentRunBoundsCm(120.0, 45.0).first);
}

TEST(HRoutePlanner, MissionTimeIncludesTakeoffAnchorTransit) {
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {150.0, 150.0}; profile.cruise_height_cm = 100.0;
    profile.descent_angle_deg = 45.0; profile.preferred_heading_deg = 0.0; profile.heading_tolerance_deg = 0.0;
    const double actual = hcore::estimateMissionTimeSeconds({"A1B1"}, 3, profile, 3);
    EXPECT_NEAR(actual, 2.0 + std::sqrt(2.0), 1e-9);
}

TEST(HRoutePlanner, RejectsTakeoffTransitAcrossNoFlyCell) {
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {150.0, 150.0}; profile.cruise_height_cm = 100.0;
    profile.descent_angle_deg = 45.0; profile.preferred_heading_deg = 0.0; profile.heading_tolerance_deg = 0.0;
    hcore::RouteRequest request{3, 3, "A1B1", {"A2B1"}, profile};
    const auto result = hcore::planRoute(request);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.route.empty());
    EXPECT_NE(result.failure_reason.find("takeoff transit"), std::string::npos);
}

TEST(HRoutePlanner, FailsWithoutLandingCompatibleTerminal) {
    auto profile = sampleLandingProfile(); profile.takeoff_anchor_cm = {10000.0, 10000.0}; profile.touchdown_radius_cm = 0.0;
    std::string error;
    EXPECT_TRUE(hcore::planRoute(9, 7, "A9B1", {"A4B3", "A5B3", "A6B3"}, profile, &error).empty());
    EXPECT_NE(error.find("landing-compatible"), std::string::npos);
}

TEST(HRoutePlanner, MatchesExactMissionTimeOnSmallGrids) {
    struct Case { int width; int height; std::string start; hcore::CellSet blocked; std::string terminal; };
    for (const Case &item : std::vector<Case>{{3,3,"A1B1",{},"A3B3"},
             {3,3,"A1B1",{"A2B2"},"A3B3"}, {4,3,"A1B1",{"A2B2"},"A4B3"}}) {
        const auto profile = landingProfileForTerminal(item.terminal, item.height);
        const auto terminals = hcore::terminalCellsForLanding(item.width, item.height, item.blocked, profile);
        ASSERT_TRUE(terminals.count(item.terminal));
        hcore::MissionTiming timing{75.0, 60.0, 55.0, 1.0, 2.0, 0.25};
        hcore::RouteRequest request{item.width, item.height, item.start, item.blocked, profile, timing};
        const auto planned = hcore::planRoute(request);
        ASSERT_TRUE(planned.ok) << planned.failure_reason;
        EXPECT_EQ(planned.search_optimality, hcore::SearchOptimality::ProvenOptimal);
        const auto exact = exactTimeOptimalRoute(item.width, item.height, item.start, item.blocked, terminals, profile, timing);
        ASSERT_FALSE(exact.empty());
        EXPECT_NEAR(planned.estimated_mission_time_s,
            hcore::estimateMissionTimeSeconds(exact, item.height, profile, item.width, item.blocked, timing), 1e-9);
    }
}

TEST(HRoutePlanner, CoversAllLegalFullSizeThreeCellBarriers) {
    std::vector<hcore::CellSet> layouts;
    for (int y = 0; y < 7; ++y) for (int x = 0; x <= 6; ++x) {
        hcore::CellSet cells{hcore::encodeCell(x,y), hcore::encodeCell(x+1,y), hcore::encodeCell(x+2,y)};
        if (!cells.count("A9B1")) layouts.push_back(cells);
    }
    for (int x = 0; x < 9; ++x) for (int y = 0; y <= 4; ++y) {
        hcore::CellSet cells{hcore::encodeCell(x,y), hcore::encodeCell(x,y+1), hcore::encodeCell(x,y+2)};
        if (!cells.count("A9B1")) layouts.push_back(cells);
    }
    for (const auto &blocked : layouts) {
        hcore::RouteRequest request{9, 7, "A9B1", blocked, sampleLandingProfile()};
        const auto expected = independentlyEnumerateTerminals(9, 7, blocked, *request.landing_profile);
        ASSERT_FALSE(expected.empty());
        const auto result = hcore::planRoute(request);
        ASSERT_TRUE(result.ok) << result.failure_reason;
        EXPECT_EQ(result.route.front(), request.start_cell);
        EXPECT_EQ(hcore::CellSet(result.route.begin(), result.route.end()).size(), 60U);
        EXPECT_TRUE(expected.count(result.route.back()));
        for (const auto &cell : blocked) EXPECT_EQ(std::count(result.route.begin(), result.route.end(), cell), 0);
        verifyAdjacentRoute(result.route);
        EXPECT_LT(result.estimated_mission_time_s, 300.0);
    }
}

TEST(HRoutePlanner, MissionTimeDoesNotPenalizeTurnsOrRepeatedVisits) {
    const double straight = hcore::estimateMissionTimeSeconds({"A1B1", "A2B1", "A3B1"});
    EXPECT_DOUBLE_EQ(hcore::estimateMissionTimeSeconds({"A1B1", "A1B2", "A2B2"}), straight);
    EXPECT_DOUBLE_EQ(hcore::estimateMissionTimeSeconds({"A1B1", "A2B1", "A1B1"}), straight);
}
