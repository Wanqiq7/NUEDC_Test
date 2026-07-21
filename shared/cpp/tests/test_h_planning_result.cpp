#include <gtest/gtest.h>

#include "h_problem_core/planning/route_planner.h"

namespace {
hcore::LandingProfile smallGridLandingProfile() {
    hcore::LandingProfile profile;
    profile.takeoff_anchor_cm = {200.0, 200.0};
    profile.cruise_height_cm = 100.0;
    profile.descent_angle_deg = 45.0;
    profile.preferred_heading_deg = 45.0;
    profile.heading_tolerance_deg = 180.0;
    return profile;
}
}

TEST(HPlanningResult, RouteRequestRequiresLandingProfile) {
    hcore::RouteRequest request{3, 3, "A1B1"};
    const auto result = hcore::planRoute(request);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.route.empty());
    EXPECT_NE(result.failure_reason.find("landing_profile"), std::string::npos);
}

TEST(HPlanningResult, ReportsCoverageAndCost) {
    hcore::RouteRequest request{3, 3, "A1B1", {"A2B2"}, smallGridLandingProfile()};
    const auto result = hcore::planRoute(request);
    ASSERT_TRUE(result.ok) << result.failure_reason;
    ASSERT_FALSE(result.route.empty());
    EXPECT_EQ(result.route.front(), "A1B1");
    EXPECT_DOUBLE_EQ(result.coverage_rate, 1.0);
    EXPECT_GT(result.cost, 0.0);
    EXPECT_DOUBLE_EQ(result.cost, result.estimated_mission_time_s);
}

TEST(HPlanningResult, ReportsConfiguredTimeAndOptimalStatus) {
    hcore::RouteRequest request{3, 3, "A1B1", {}, smallGridLandingProfile()};
    request.mission_timing = {50.0, 50.0, 50.0, 2.0, 3.0, 0.5};
    const auto result = hcore::planRoute(request);
    ASSERT_TRUE(result.ok) << result.failure_reason;
    EXPECT_GT(result.estimated_mission_time_s, 0.0);
    EXPECT_DOUBLE_EQ(result.cost, result.estimated_mission_time_s);
    EXPECT_EQ(result.search_optimality, hcore::SearchOptimality::ProvenOptimal);
}

TEST(HPlanningResult, ReportsBlockedStartFailure) {
    hcore::RouteRequest request{3, 3, "A1B1", {"A1B1"}, smallGridLandingProfile()};
    const auto result = hcore::planRoute(request);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.route.empty());
    EXPECT_DOUBLE_EQ(result.coverage_rate, 0.0);
    EXPECT_NE(result.failure_reason.find("start"), std::string::npos);
}
