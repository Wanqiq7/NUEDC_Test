#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hcore {

struct PointCm {
    double x_cm = 0.0;
    double y_cm = 0.0;
};

struct GridPoint {
    int x = 0;
    int y = 0;
};

using CellList = std::vector<std::string>;
using CellSet = std::set<std::string>;

struct Animal {
    std::string cell;
    std::string name;
    std::uint32_t count = 0;
};

struct LandingProfile {
    PointCm takeoff_anchor_cm;
    double cruise_height_cm = 0.0;
    double descent_angle_deg = 0.0;
    double descent_angle_tolerance_deg = 5.0;
    double touchdown_radius_cm = 0.0;
    double preferred_heading_deg = 45.0;
    double heading_tolerance_deg = 35.0;
};

struct MissionTiming {
    // 赛题未规定速度与识别等待时间，默认值必须按实机标定后覆盖。
    double cruise_speed_cm_per_s = 100.0;
    double ascent_speed_cm_per_s = 100.0;
    double descent_speed_cm_per_s = 100.0;
    double takeoff_fixed_time_s = 0.0;
    double landing_fixed_time_s = 0.0;
    double per_cell_dwell_time_s = 0.0;
};

struct CaseConfig {
    std::string case_id;
    std::string start_cell;
    CellList no_fly_cells;
    int tick_interval_ms = 100;
    std::vector<Animal> animals;
    std::optional<LandingProfile> landing;
    MissionTiming mission_timing;
};

} // namespace hcore
