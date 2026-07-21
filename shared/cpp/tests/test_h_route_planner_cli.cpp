#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "h_problem_core/tools/planner_cli.h"

TEST(HRoutePlannerCli, PlansCanonicalTask) {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["A2B2","A2B3","A2B4"]})");
    ASSERT_EQ(result.exit_code, 0);
    const auto output = nlohmann::json::parse(result.stdout_bytes);
    EXPECT_TRUE(output.at("ok").get<bool>());
    EXPECT_EQ(output.at("plan").at("message_type"), "task_plan");
    EXPECT_EQ(output.at("plan").at("terminal_waypoint_id"), "touchdown");
}

TEST(HRoutePlannerCli, RejectsUnknownSchema) {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"wrong","case_path":"shared/cases/sample_case.json","no_fly_cells":[]})");
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_EQ(nlohmann::json::parse(result.stdout_bytes).at("error_code"), "invalid_request");
}

TEST(HRoutePlannerCli, RejectsInvalidNoFlyCell) {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["invalid"]})");
    EXPECT_EQ(result.exit_code, 3);
    EXPECT_FALSE(nlohmann::json::parse(result.stdout_bytes).at("ok").get<bool>());
}

TEST(HRoutePlannerCli, RejectsMissingCase) {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/missing.json","no_fly_cells":[]})");
    EXPECT_EQ(result.exit_code, 3);
    const auto output = nlohmann::json::parse(result.stdout_bytes);
    EXPECT_EQ(output.at("error_code"), "case_load_failed");
    EXPECT_TRUE(result.stderr_bytes.empty() || result.stderr_bytes.find('{') == std::string::npos);
}
