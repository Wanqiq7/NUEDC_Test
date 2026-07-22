#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"

namespace {

class UniqueTempDirectory {
public:
    UniqueTempDirectory() {
        static std::atomic<unsigned int> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path()
                / ("h_case_loader_" + std::to_string(::getpid()) + "_"
                    + std::to_string(timestamp) + "_"
                    + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                break;
            }
            if (error != std::errc::file_exists) {
                throw std::system_error(error, "create temporary test directory");
            }
        }
    }

    ~UniqueTempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

nlohmann::json minimalCase() {
    return {{"case_id", "minimal"}, {"start_cell", "A1B1"}};
}

nlohmann::json validLanding() {
    return {
        {"takeoff_anchor_cm", {450.0, 50.0}},
        {"cruise_height_cm", 120.0},
        {"descent_angle_deg", 45.0},
        {"touchdown_radius_cm", 20.0},
    };
}

} // namespace

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
    const UniqueTempDirectory temporary_directory;
    const auto path = temporary_directory.path() / "closed_route_case.json";
    { std::ofstream file(path); file << R"({"case_id":"closed-route","start_cell":"A1B1","return_to_start":true})"; }
    std::string error;
    const auto loaded = hcore::loadCase(path, &error);
    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(error.find("return_to_start"), std::string::npos);
}

TEST(HCaseLoader, DefaultsTickIntervalWhenJsonIntegerIsOutsideIntRange) {
    for (const nlohmann::json value : {
             nlohmann::json(std::numeric_limits<std::uint64_t>::max()),
             nlohmann::json(static_cast<std::int64_t>(std::numeric_limits<int>::min()) - 1),
             nlohmann::json(static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1),
         }) {
        auto object = minimalCase();
        object["tick_interval_ms"] = value;
        std::string error;
        const auto loaded = hcore::caseFromJsonObject(object, &error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->tick_interval_ms, 100);
    }
}

TEST(HCaseLoader, LoadsRepresentableSignedAndUnsignedTickIntervals) {
    for (const nlohmann::json value : {nlohmann::json(-12), nlohmann::json(250U)}) {
        auto object = minimalCase();
        object["tick_interval_ms"] = value;
        std::string error;
        const auto loaded = hcore::caseFromJsonObject(object, &error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->tick_interval_ms, value.get<int>());
    }
}

TEST(HCaseLoader, IgnoresAnimalsWhenValueIsNotAnArray) {
    for (const nlohmann::json value : {
             nlohmann::json(nullptr),
             nlohmann::json("not-an-array"),
             nlohmann::json::object(),
         }) {
        auto object = minimalCase();
        object["animals"] = value;
        std::string error;
        const auto loaded = hcore::caseFromJsonObject(object, &error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_TRUE(loaded->animals.empty());
    }
}

TEST(HCaseLoader, ParsesAnimalObjectsAndRejectsInvalidEntriesInArray) {
    auto valid = minimalCase();
    valid["animals"] = {{{"cell", "A1B2"}, {"name", "deer"}, {"count", 2}}};
    std::string error;
    const auto loaded = hcore::caseFromJsonObject(valid, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    ASSERT_EQ(loaded->animals.size(), 1U);
    EXPECT_EQ(loaded->animals.front().cell, "A1B2");

    for (const nlohmann::json animal : {
             nlohmann::json(nullptr),
             nlohmann::json::object(),
             nlohmann::json{{"name", "deer"}},
             nlohmann::json{{"cell", "A1B2"}},
         }) {
        auto invalid = minimalCase();
        invalid["animals"] = nlohmann::json::array({animal});
        error.clear();
        EXPECT_FALSE(hcore::caseFromJsonObject(invalid, &error).has_value())
            << animal.dump();
        EXPECT_FALSE(error.empty()) << animal.dump();
    }
}

TEST(HCaseLoader, IgnoresLandingWhenValueIsNotAnObject) {
    for (const nlohmann::json value : {
             nlohmann::json(nullptr),
             nlohmann::json("not-an-object"),
             nlohmann::json::array(),
         }) {
        auto object = minimalCase();
        object["landing"] = value;
        std::string error;
        const auto loaded = hcore::caseFromJsonObject(object, &error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_FALSE(loaded->landing.has_value());
    }
}

TEST(HCaseLoader, ParsesLandingObjectAndRejectsObjectMissingRequiredValues) {
    auto valid = minimalCase();
    valid["landing"] = validLanding();
    std::string error;
    const auto loaded = hcore::caseFromJsonObject(valid, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_TRUE(loaded->landing.has_value());

    auto invalid = minimalCase();
    invalid["landing"] = nlohmann::json::object();
    EXPECT_FALSE(hcore::caseFromJsonObject(invalid, &error).has_value());
    EXPECT_NE(error.find("takeoff_anchor_cm"), std::string::npos);
}

TEST(HCaseLoader, ReportsDistinctMissingAndDirectoryOpenErrors) {
    const UniqueTempDirectory temporary_directory;
    std::string missing_error;
    EXPECT_FALSE(hcore::loadCase(temporary_directory.path() / "missing.json", &missing_error).has_value());
    EXPECT_NE(missing_error.find("No such file or directory"), std::string::npos);

    std::string directory_error;
    EXPECT_FALSE(hcore::loadCase(temporary_directory.path(), &directory_error).has_value());
    EXPECT_NE(directory_error.find("Is a directory"), std::string::npos);
    EXPECT_NE(directory_error, missing_error);
}

TEST(HCaseLoader, ReportsPermissionDeniedOpenError) {
    const UniqueTempDirectory temporary_directory;
    const auto path = temporary_directory.path() / "unreadable.json";
    { std::ofstream file(path); file << minimalCase().dump(); }
    std::filesystem::permissions(path, std::filesystem::perms::none);

    std::string error;
    EXPECT_FALSE(hcore::loadCase(path, &error).has_value());
    EXPECT_NE(error.find("Permission denied"), std::string::npos);
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
    EXPECT_DOUBLE_EQ(plan->waypoints.back().x, 0.1707629936490912);
    EXPECT_DOUBLE_EQ(plan->waypoints.back().y, 0.05692099788303039);
    EXPECT_DOUBLE_EQ(plan->waypoints.back().z, 0.0);
    EXPECT_EQ(plan->waypoints.back().payload_json, R"({"touchdown":true})");

    const auto &descent_start = plan->waypoints[plan->waypoints.size() - 2];
    EXPECT_EQ(descent_start.id, "A8B4");
    EXPECT_EQ(descent_start.action, "navigate");
    EXPECT_DOUBLE_EQ(descent_start.x, 1.5);
    EXPECT_DOUBLE_EQ(descent_start.y, 0.5);
    EXPECT_DOUBLE_EQ(descent_start.z, 1.2);
    EXPECT_EQ(descent_start.payload_json, R"({"cell":"A8B4"})");

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
