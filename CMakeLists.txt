cmake_minimum_required(VERSION 3.16)

project(HWildlifeMixedTestbed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

foreach(shell_test
    test_start_ground_hotspot
    test_check_ground_control_network
)
    add_test(
        NAME ${shell_test}
        COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/tests/${shell_test}.sh
    )
endforeach()
add_test(
    NAME test_web_ground_station_scripts
    COMMAND bash ${PROJECT_SOURCE_DIR}/web_ground_station/tests/test_scripts.sh
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)

add_subdirectory(shared/cpp)
