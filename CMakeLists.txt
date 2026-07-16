cmake_minimum_required(VERSION 3.16)

project(HWildlifeMixedTestbed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

foreach(shell_test
    test_start_ground_hotspot
    test_check_ground_control_network
    test_start_ground_integration
)
    add_test(
        NAME ${shell_test}
        COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/tests/${shell_test}.sh
    )
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Test)
find_package(protobuf CONFIG QUIET)
if(protobuf_FOUND)
    set(NUEDC_PROTOC_EXECUTABLE protobuf::protoc)
else()
    find_package(Protobuf REQUIRED)
    find_program(NUEDC_PROTOC_EXECUTABLE protoc REQUIRED)
endif()
find_library(ZMQ_LIBRARY zmq REQUIRED)
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp PATHS /usr/include /usr/local/include REQUIRED)

set(PROTO_FILE ${PROJECT_SOURCE_DIR}/shared/proto/messages.proto)
set(CPP_PROTO_DIR ${PROJECT_BINARY_DIR}/generated/proto)

add_custom_command(
    OUTPUT ${CPP_PROTO_DIR}/messages.pb.cc ${CPP_PROTO_DIR}/messages.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CPP_PROTO_DIR}
    COMMAND ${NUEDC_PROTOC_EXECUTABLE}
            --cpp_out=${CPP_PROTO_DIR}
            -I ${PROJECT_SOURCE_DIR}/shared/proto
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    VERBATIM
)

add_library(proto_messages STATIC
    ${CPP_PROTO_DIR}/messages.pb.cc
)
set_target_properties(proto_messages PROPERTIES AUTOMOC OFF)

target_include_directories(proto_messages PUBLIC
    ${CPP_PROTO_DIR}
)

target_link_libraries(proto_messages PUBLIC protobuf::libprotobuf)

add_subdirectory(shared/cpp)
add_subdirectory(ground_station_computer)
