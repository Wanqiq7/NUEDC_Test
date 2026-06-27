set(AIRBORNE_SOURCES
    src/airborne_runtime.cpp
    src/command_server.cpp
    src/mission_runtime_factory.cpp
)

add_executable(airborne_app
    src/main.cpp
    ${AIRBORNE_SOURCES}
)

target_include_directories(airborne_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CPPZMQ_INCLUDE_DIR}
)

target_link_libraries(airborne_app PRIVATE
    Qt6::Core
    h_problem_core
    ${ZMQ_LIBRARY}
)

function(add_airborne_test target_name test_source)
add_executable(${target_name}
    ${test_source}
    ${AIRBORNE_SOURCES}
)
target_include_directories(${target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CPPZMQ_INCLUDE_DIR}
)
target_link_libraries(${target_name} PRIVATE
    Qt6::Core
    Qt6::Test
    h_problem_core
    ${ZMQ_LIBRARY}
)
add_test(NAME ${target_name} COMMAND ${target_name})
set_tests_properties(${target_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
endfunction()

add_airborne_test(test_airborne_runtime tests/test_airborne_runtime.cpp)
