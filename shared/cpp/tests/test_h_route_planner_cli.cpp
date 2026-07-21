#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>

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

TEST(HRoutePlannerCli, PreservesQStringCompatibleNoFlyCellClassification) {
    for (const char *cell : {"A+1B1", "A 1B1", "A1B1 "}) {
        SCOPED_TRACE(cell);
        const nlohmann::json request{
            {"schema", "h_planning_request_v1"},
            {"case_path", "shared/cases/sample_case.json"},
            {"no_fly_cells", {cell}},
        };
        const auto result = hcore::runPlannerCliRequest(request.dump());
        const auto output = nlohmann::json::parse(result.stdout_bytes);
        EXPECT_NE(output.value("error_code", ""), "invalid_no_fly_zone");
    }
}

TEST(HRoutePlannerCli, RejectsMissingCase) {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/missing.json","no_fly_cells":[]})");
    EXPECT_EQ(result.exit_code, 3);
    const auto output = nlohmann::json::parse(result.stdout_bytes);
    EXPECT_EQ(output.at("error_code"), "case_load_failed");
    EXPECT_TRUE(result.stderr_bytes.empty() || result.stderr_bytes.find('{') == std::string::npos);
}

TEST(HRoutePlannerCli, ClassifiesDirectoryCasePathAsLoadFailure) {
    const nlohmann::json request{
        {"schema", "h_planning_request_v1"},
        {"case_path", std::filesystem::temp_directory_path().string()},
        {"no_fly_cells", nlohmann::json::array()},
    };
    const auto result = hcore::runPlannerCliRequest(request.dump());
    ASSERT_EQ(result.exit_code, 3);
    const auto output = nlohmann::json::parse(result.stdout_bytes);
    EXPECT_EQ(output.at("error_code"), "case_load_failed");
    EXPECT_NE(output.at("message").get<std::string>().find("Is a directory"), std::string::npos);
}
