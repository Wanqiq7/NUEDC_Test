cmake_minimum_required(VERSION 3.16)

project(HWildlifeMixedTestbed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Test)
find_package(Protobuf REQUIRED)
find_library(ZMQ_LIBRARY zmq REQUIRED)
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp PATHS /usr/include /usr/local/include REQUIRED)

set(PROTO_FILE ${PROJECT_SOURCE_DIR}/proto/messages.proto)
set(CPP_PROTO_DIR ${PROJECT_BINARY_DIR}/generated/proto)
set(PY_PROTO_DIR ${PROJECT_SOURCE_DIR}/python/uav_testbed/generated)

add_custom_command(
    OUTPUT ${CPP_PROTO_DIR}/messages.pb.cc ${CPP_PROTO_DIR}/messages.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CPP_PROTO_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${CPP_PROTO_DIR}
            -I ${PROJECT_SOURCE_DIR}/proto
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    VERBATIM
)

add_custom_command(
    OUTPUT ${PY_PROTO_DIR}/messages_pb2.py
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PY_PROTO_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --python_out=${PY_PROTO_DIR}
            -I ${PROJECT_SOURCE_DIR}/proto
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    VERBATIM
)

add_library(proto_messages STATIC
    ${CPP_PROTO_DIR}/messages.pb.cc
)

target_include_directories(proto_messages PUBLIC
    ${CPP_PROTO_DIR}
)

target_link_libraries(proto_messages PUBLIC protobuf::libprotobuf)

add_custom_target(generate_python_proto ALL
    DEPENDS ${PY_PROTO_DIR}/messages_pb2.py
)

add_subdirectory(ground_station)
