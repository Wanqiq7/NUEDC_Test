set(COMPETITION_CORE_SOURCES
    src/storage/json_codec.cpp
    src/task/task_plan_store.cpp
    src/protocol/competition_command_handler.cpp
    src/protocol/envelope_codec.cpp
)

add_library(competition_core STATIC
    ${COMPETITION_CORE_SOURCES}
)

target_include_directories(competition_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(competition_core PUBLIC
    Qt6::Core
    proto_messages
)

set(H_PROBLEM_CORE_SOURCES
    src/mission/case_loader.cpp
    src/mission/mission_planning.cpp
    src/planning/mission_geometry.cpp
    src/planning/route_cost.cpp
    src/planning/route_planner.cpp
    src/tools/planner_cli.cpp
)

add_library(h_problem_core STATIC
    ${H_PROBLEM_CORE_SOURCES}
)
set_target_properties(h_problem_core PROPERTIES AUTOMOC OFF)

target_include_directories(h_problem_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(h_problem_core PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(h_route_planner_cli tools/h_route_planner_cli_main.cpp)
set_target_properties(h_route_planner_cli PROPERTIES AUTOMOC OFF)
target_link_libraries(h_route_planner_cli PRIVATE
    h_problem_core
)

function(add_competition_core_test target_name test_source)
add_executable(${target_name}
    ${test_source}
)
target_link_libraries(${target_name} PRIVATE
    Qt6::Core
    Qt6::Test
    competition_core
)
add_test(NAME ${target_name} COMMAND ${target_name})
set_tests_properties(${target_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
endfunction()

function(add_h_problem_core_test target_name test_source)
add_executable(${target_name}
    ${test_source}
)
set_target_properties(${target_name} PROPERTIES AUTOMOC OFF)
target_link_libraries(${target_name} PRIVATE
    GTest::gtest_main
    h_problem_core
)
add_test(NAME ${target_name} COMMAND ${target_name})
set_tests_properties(${target_name} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

add_competition_core_test(test_task_protocol tests/test_task_protocol.cpp)
add_competition_core_test(test_task_ports tests/test_task_ports.cpp)
add_competition_core_test(test_json_codec tests/test_json_codec.cpp)

add_h_problem_core_test(test_h_case_loader tests/test_h_case_loader.cpp)
add_h_problem_core_test(test_h_mission_geometry tests/test_h_mission_geometry.cpp)
add_h_problem_core_test(test_h_route_planner tests/test_h_route_planner.cpp)
add_h_problem_core_test(test_h_planning_result tests/test_h_planning_result.cpp)
add_h_problem_core_test(test_h_route_planner_cli tests/test_h_route_planner_cli.cpp)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME test_h_route_planner_golden
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_planner_golden.py
            $<TARGET_FILE:h_route_planner_cli>
            ${PROJECT_SOURCE_DIR}/shared/cases/golden/planner_cli_cases.json
)
set_tests_properties(test_h_route_planner_golden PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
