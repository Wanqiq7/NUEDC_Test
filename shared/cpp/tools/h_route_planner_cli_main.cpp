#include "h_problem_core/tools/planner_cli.h"

#include <iostream>
#include <iterator>
#include <string>

int main() {
    const std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    const auto result = hcore::runPlannerCliRequest(input);
    std::cout.write(result.stdout_bytes.data(), result.stdout_bytes.size());
    std::cerr.write(result.stderr_bytes.data(), result.stderr_bytes.size());
    return result.exit_code;
}
