#pragma once

#include <string>
#include <string_view>

namespace hcore {

struct PlannerCliResult {
    int exit_code = 4;
    std::string stdout_bytes;
    std::string stderr_bytes;
};

PlannerCliResult runPlannerCliRequest(std::string_view request_bytes);

} // namespace hcore
