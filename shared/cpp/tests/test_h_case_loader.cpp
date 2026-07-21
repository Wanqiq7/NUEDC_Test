#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"

TEST(HCaseLoader, ParsesSampleCase) {
    std::string error;
    const auto loaded = hcore::loadCase("shared/cases/sample_case.json", &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_EQ(loaded->case_id, "wildlife-demo");
    EXPECT_EQ(loaded->start_cell, "A9B1");
    EXPECT_EQ(loaded->no_fly_cells, (hcore::CellList{"A4B3", "A5B3", "A6B3"}));
    EXPECT_EQ(loaded->tick_interval_ms, 150);
    EXPECT_EQ(loaded->animals.size(), 4U);
    ASSERT_TRUE(loaded->landing.has_value());
    EXPECT_DOUBLE_EQ(loaded->landing->takeoff_anchor_cm.x_cm, 450.0);
    EXPECT_DOUBLE_EQ(loaded->landing->descent_angle_deg, 45.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.cruise_speed_cm_per_s, 125.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.ascent_speed_cm_per_s, 80.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.descent_speed_cm_per_s, 70.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.takeoff_fixed_time_s, 2.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.landing_fixed_time_s, 3.0);
    EXPECT_DOUBLE_EQ(loaded->mission_timing.per_cell_dwell_time_s, 0.1);
}

TEST(HCaseLoader, RejectsDeprecatedReturnToStartConfiguration) {
    const auto path = std::filesystem::temp_directory_path() / "h_closed_route_case.json";
    { std::ofstream file(path); file << R"({"case_id":"closed-route","start_cell":"A1B1","return_to_start":true})"; }
    std::string error;
    const auto loaded = hcore::loadCase(path, &error);
    std::filesystem::remove(path);
    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(error.find("return_to_start"), std::string::npos);
}

TEST(HCaseLoader, BuildTaskPlanRejectsMissingLandingProfile) {
    hcore::CaseConfig config;
    config.case_id = "missing-landing";
    config.start_cell = "A1B1";
    std::string error;
    EXPECT_FALSE(hcore::buildTaskPlan(config, {}, &error).has_value());
    EXPECT_NE(error.find("landing"), std::string::npos);
}

TEST(HCaseLoader, BuildTaskPlanRejectsNonCanonicalStart) {
    std::string error;
    auto config = hcore::loadCase("shared/cases/sample_case.json", &error);
    ASSERT_TRUE(config.has_value()) << error;
    config->start_cell = "A8B1";
    EXPECT_FALSE(hcore::buildTaskPlan(*config, {}, &error).has_value());
    EXPECT_NE(error.find("A9B1"), std::string::npos);
}

TEST(HCaseLoader, BuildTaskPlanPropagatesMissionTiming) {
    std::string error;
    auto config = hcore::loadCase("shared/cases/sample_case.json", &error);
    ASSERT_TRUE(config.has_value()) << error;
    config->mission_timing.cruise_speed_cm_per_s = 0.0;
    EXPECT_FALSE(hcore::buildTaskPlan(*config, {}, &error).has_value());
    EXPECT_NE(error.find("mission timing"), std::string::npos);
}

TEST(HCaseLoader, BuildTaskPlanProducesCanonicalPlanWithLandingMetadata) {
    std::string error;
    const auto config = hcore::loadCase("shared/cases/sample_case.json", &error);
    ASSERT_TRUE(config.has_value()) << error;
    const auto plan = hcore::buildTaskPlan(*config, {}, &error);
    ASSERT_TRUE(plan.has_value()) << error;
    EXPECT_EQ(plan->task_id, config->case_id);
    EXPECT_EQ(plan->task_type, "h_problem");
    EXPECT_EQ(plan->start_waypoint_id, "A9B1");
    EXPECT_EQ(plan->terminal_waypoint_id, "touchdown");
    ASSERT_GE(plan->waypoints.size(), 3U);
    EXPECT_EQ(plan->waypoints.front().id, "A9B1");
    EXPECT_EQ(plan->waypoints.front().action, "takeoff");
    EXPECT_DOUBLE_EQ(plan->waypoints.front().x, 0.0);
    EXPECT_DOUBLE_EQ(plan->waypoints.front().y, 0.0);
    EXPECT_DOUBLE_EQ(plan->waypoints.front().z, 1.2);
    EXPECT_EQ(plan->waypoints.back().id, "touchdown");
    EXPECT_EQ(plan->waypoints.back().action, "land");
    EXPECT_DOUBLE_EQ(plan->waypoints.back().z, 0.0);
    EXPECT_NE(plan->waypoints[plan->waypoints.size() - 2].id, plan->waypoints.back().id);

    const auto metadata = nlohmann::json::parse(plan->metadata_json);
    EXPECT_EQ(metadata.at("execution_contract"), "h_field_m_v1");
    EXPECT_DOUBLE_EQ(metadata.at("cruise_height_cm").get<double>(), 120.0);
    EXPECT_EQ(metadata.at("terminal_cell"), plan->waypoints[plan->waypoints.size() - 2].id);
    for (const char *field : {"case_id", "start_cell", "no_fly_cells", "terminal_cell",
             "landing_enabled", "descent_angle_deg", "takeoff_anchor_x_cm",
             "takeoff_anchor_y_cm", "touchdown_x_cm", "touchdown_y_cm", "descent_run_cm",
             "descent_heading_deg", "estimated_mission_time_s", "planning_optimality",
             "planning_warnings", "execution_contract", "cruise_height_cm"}) {
        EXPECT_TRUE(metadata.contains(field)) << field;
    }
}
