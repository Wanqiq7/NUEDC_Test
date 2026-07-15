set(GROUND_STATION_CORE_SOURCES
    src/framework/communication/command_link_health.cpp
    src/framework/communication/command_link_monitor.cpp
    src/framework/communication/envelope_codec.cpp
    src/framework/communication/reliable_command_client.cpp
    src/framework/communication/serialized_command_transport.cpp
    src/framework/communication/zmq_command_client.cpp
    src/framework/config/network_config.cpp
    src/framework/config/repository_paths.cpp
    src/framework/runtime/airborne_sync_state.cpp
    src/framework/runtime/mission_runtime_state.cpp
    src/h_problem/mission/h_mission_command_service.cpp
    src/h_problem/mission/h_mission_controller.cpp
    src/h_problem/mission/h_no_fly_planning_state.cpp
    src/h_problem/mission/h_protocol_adapter.cpp
    src/h_problem/mission/h_route_planner_bridge.cpp
    src/h_problem/rules/h_grid_mapper.cpp
    src/h_problem/rules/h_no_fly_zone_rules.cpp
    src/h_problem/rules/h_route_validator.cpp
    src/h_problem/storage/h_detection_repository.cpp
    src/h_problem/ui/h_route_visualizer.cpp
)

set(GROUND_STATION_APP_SOURCES
    src/framework/task/competition_task_adapter.h
    src/framework/task/competition_task_registry.cpp
    src/app/main_window.cpp
    src/framework/communication/message_dispatcher.cpp
    src/framework/communication/zmq_subscriber_worker.cpp
    src/h_problem/ui/h_grid_scene.cpp
    src/h_problem/ui/h_problem_view.cpp
    src/h_problem/ui/h_problem_page.cpp
)

set(GROUND_STATION_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

function(add_ground_station_test target_name test_source)
add_executable(${target_name}
    ${test_source}
    ${GROUND_STATION_CORE_SOURCES}
)

target_include_directories(${target_name} PRIVATE
    ${GROUND_STATION_INCLUDE_DIR}
)

target_link_libraries(${target_name} PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
    Qt6::Widgets
    h_problem_core
    protobuf::libprotobuf
    proto_messages
    ${ZMQ_LIBRARY}
)

add_test(NAME ${target_name} COMMAND ${target_name})
set_tests_properties(${target_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
endfunction()

add_ground_station_test(test_detection_repository tests/test_detection_repository.cpp)
add_ground_station_test(test_grid_mapper tests/test_grid_mapper.cpp)
add_ground_station_test(test_network_config tests/test_network_config.cpp)
add_ground_station_test(test_repository_paths tests/test_repository_paths.cpp)
add_ground_station_test(test_route_validator tests/test_route_validator.cpp)
add_ground_station_test(test_route_visualizer tests/test_route_visualizer.cpp)
add_ground_station_test(test_no_fly_zone_rules tests/test_no_fly_zone_rules.cpp)
add_ground_station_test(test_planning_state_machine tests/test_planning_state_machine.cpp)
add_ground_station_test(test_zmq_command_client tests/test_zmq_command_client.cpp)
add_ground_station_test(test_zmq_command_client_integration tests/test_zmq_command_client_integration.cpp)
add_ground_station_test(test_zmq_subscriber_worker tests/test_zmq_subscriber_worker.cpp)
target_sources(test_zmq_subscriber_worker PRIVATE
    src/framework/communication/zmq_subscriber_worker.cpp
)
add_ground_station_test(test_reliable_command_client tests/test_reliable_command_client.cpp)
add_ground_station_test(test_command_link_health tests/test_command_link_health.cpp)
add_ground_station_test(test_command_link_monitor tests/test_command_link_monitor.cpp)
add_ground_station_test(test_architecture_boundaries tests/test_architecture_boundaries.cpp)
# 注入源码根目录（= 顶层 project() 的 Ground/），使边界测试不依赖运行时工作目录，
# 直接运行测试可执行文件也能正确定位待扫描的源文件。
target_compile_definitions(test_architecture_boundaries PRIVATE
    GROUND_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_ground_station_test(test_envelope_codec tests/test_envelope_codec.cpp)
add_ground_station_test(test_mission_command_service tests/test_mission_command_service.cpp)
add_ground_station_test(test_mission_runtime_state tests/test_mission_runtime_state.cpp)
add_ground_station_test(test_airborne_sync_state tests/test_airborne_sync_state.cpp)
add_ground_station_test(test_h_mission_controller tests/test_h_mission_controller.cpp)
add_ground_station_test(test_grid_scene tests/test_grid_scene.cpp)
target_sources(test_grid_scene PRIVATE
    src/h_problem/ui/h_grid_scene.cpp
)
add_ground_station_test(test_main_window tests/test_main_window.cpp)
target_sources(test_main_window PRIVATE
    ${GROUND_STATION_APP_SOURCES}
)

add_executable(ground_station_app
    src/app/main.cpp
    ${GROUND_STATION_CORE_SOURCES}
    ${GROUND_STATION_APP_SOURCES}
)

target_include_directories(ground_station_app PRIVATE
    ${GROUND_STATION_INCLUDE_DIR}
    ${CPPZMQ_INCLUDE_DIR}
)

target_link_libraries(ground_station_app PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Widgets
    h_problem_core
    proto_messages
    protobuf::libprotobuf
    ${ZMQ_LIBRARY}
)
