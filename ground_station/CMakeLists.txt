set(GROUND_STATION_CORE_SOURCES
    src/detection_repository.cpp
    src/grid_mapper.cpp
    src/route_validator.cpp
    src/route_visualizer.cpp
    src/no_fly_zone_rules.cpp
    src/planning_state_machine.cpp
    src/mission_plan_bridge.cpp
)

set(GROUND_STATION_APP_SOURCES
    src/grid_scene.cpp
    src/main_window.cpp
    src/message_dispatcher.cpp
    src/zmq_subscriber_worker.cpp
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
    protobuf::libprotobuf
    proto_messages
    ${ZMQ_LIBRARY}
)

add_test(NAME ${target_name} COMMAND ${target_name})
set_tests_properties(${target_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_ground_station_test(test_detection_repository tests/test_detection_repository.cpp)
add_ground_station_test(test_grid_mapper tests/test_grid_mapper.cpp)
add_ground_station_test(test_route_validator tests/test_route_validator.cpp)
add_ground_station_test(test_route_visualizer tests/test_route_visualizer.cpp)
add_ground_station_test(test_no_fly_zone_rules tests/test_no_fly_zone_rules.cpp)
add_ground_station_test(test_planning_state_machine tests/test_planning_state_machine.cpp)
add_ground_station_test(test_mission_plan_bridge tests/test_mission_plan_bridge.cpp)
add_ground_station_test(test_grid_scene tests/test_grid_scene.cpp)
target_sources(test_grid_scene PRIVATE
    src/grid_scene.cpp
)
add_ground_station_test(test_main_window tests/test_main_window.cpp)
target_sources(test_main_window PRIVATE
    ${GROUND_STATION_APP_SOURCES}
)

add_executable(ground_station_app
    src/main.cpp
    ${GROUND_STATION_CORE_SOURCES}
    ${GROUND_STATION_APP_SOURCES}
)

target_include_directories(ground_station_app PRIVATE
    ${GROUND_STATION_INCLUDE_DIR}
    ${CPPZMQ_INCLUDE_DIR}
)

add_dependencies(ground_station_app generate_python_proto)

target_link_libraries(ground_station_app PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Widgets
    proto_messages
    protobuf::libprotobuf
    ${ZMQ_LIBRARY}
)
