cmake_minimum_required(VERSION 3.16)

project(nuedc_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(Protobuf REQUIRED)
find_library(ZMQ_LIBRARY zmq REQUIRED)
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp PATHS /usr/include /usr/local/include REQUIRED)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/messages.proto)
set(GENERATED_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_custom_command(
    OUTPUT ${GENERATED_PROTO_DIR}/messages.pb.cc ${GENERATED_PROTO_DIR}/messages.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_PROTO_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${GENERATED_PROTO_DIR}
            -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    VERBATIM
)

add_library(nuedc_bridge_proto STATIC
    ${GENERATED_PROTO_DIR}/messages.pb.cc
)
target_include_directories(nuedc_bridge_proto PUBLIC
    ${GENERATED_PROTO_DIR}
)
target_link_libraries(nuedc_bridge_proto protobuf::libprotobuf)

add_library(nuedc_bridge_core STATIC
    src/bridge_conversions.cpp
)
target_include_directories(nuedc_bridge_core PUBLIC
    include
)
target_link_libraries(nuedc_bridge_core
    nuedc_bridge_proto
)
ament_target_dependencies(nuedc_bridge_core
    nav_msgs
    vision_msgs
)

add_executable(nuedc_bridge_node
    src/nuedc_bridge_node.cpp
)
target_include_directories(nuedc_bridge_node PRIVATE
    ${CPPZMQ_INCLUDE_DIR}
)
target_link_libraries(nuedc_bridge_node
    nuedc_bridge_core
    ${ZMQ_LIBRARY}
)
ament_target_dependencies(nuedc_bridge_node
    rclcpp
    nav_msgs
    vision_msgs
)

ament_add_gtest(test_bridge_conversions
    test/test_bridge_conversions.cpp
)
target_link_libraries(test_bridge_conversions
    nuedc_bridge_core
)

install(TARGETS
    nuedc_bridge_node
    DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch
    DESTINATION share/${PROJECT_NAME}
)

ament_package()
