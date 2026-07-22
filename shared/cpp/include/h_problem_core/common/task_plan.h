#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hcore {

struct TaskWaypoint {
    std::string id;
    std::uint32_t sequence_index = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::string action;
    std::string payload_json;
};

struct TaskPlan {
    std::string task_id;
    std::string task_type;
    std::string start_waypoint_id;
    std::string terminal_waypoint_id;
    std::vector<TaskWaypoint> waypoints;
    std::string metadata_json;
};

} // namespace hcore
