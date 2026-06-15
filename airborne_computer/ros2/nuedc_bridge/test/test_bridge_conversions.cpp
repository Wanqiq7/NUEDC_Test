#include <gtest/gtest.h>

#include "nuedc_bridge/bridge_conversions.h"

#include <nav_msgs/msg/odometry.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>

namespace {

nav_msgs::msg::Odometry makeOdometry() {
    nav_msgs::msg::Odometry odom;
    odom.header.frame_id = "camera_init";
    odom.pose.pose.position.x = 1.25;
    odom.pose.pose.position.y = -2.5;
    odom.pose.pose.position.z = 0.75;
    odom.twist.twist.linear.x = 0.1;
    odom.twist.twist.linear.y = 0.2;
    odom.twist.twist.linear.z = -0.3;
    return odom;
}

vision_msgs::msg::Detection2DArray makeDetections() {
    vision_msgs::msg::Detection2DArray array;
    vision_msgs::msg::Detection2D detection;
    detection.bbox.center.position.x = 320.0;
    detection.bbox.center.position.y = 240.0;
    detection.results.resize(1);
    detection.results.front().hypothesis.class_id = "rabbit";
    detection.results.front().hypothesis.score = 0.92;
    array.detections.push_back(detection);
    return array;
}

} // namespace

TEST(BridgeConversions, ConvertsOdometryToTelemetryEvent) {
    const TaskEventMessage event = nuedc_bridge::odometryToTelemetryEvent(
        makeOdometry(),
        "mission-1",
        7);

    EXPECT_EQ(event.task_id(), "mission-1");
    EXPECT_EQ(event.event_type(), "telemetry");
    EXPECT_EQ(event.sequence_index(), 7u);
    EXPECT_EQ(event.waypoint_id(), "odom");
    EXPECT_NE(event.payload_json().find("\"current_cell\":\"odom\""), std::string::npos);
    EXPECT_NE(event.payload_json().find("\"position_x\":1.25"), std::string::npos);
    EXPECT_NE(event.payload_json().find("\"linear_z\":-0.3"), std::string::npos);
}

TEST(BridgeConversions, ConvertsDetectionArrayToDetectionEvent) {
    const auto event = nuedc_bridge::detectionArrayToDetectionEvent(
        makeDetections(),
        "mission-1",
        "odom");

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->task_id(), "mission-1");
    EXPECT_EQ(event->event_type(), "detection");
    EXPECT_EQ(event->waypoint_id(), "odom");
    EXPECT_NE(event->payload_json().find("\"animal_name\":\"rabbit\""), std::string::npos);
    EXPECT_NE(event->payload_json().find("\"count\":1"), std::string::npos);
    EXPECT_NE(event->payload_json().find("\"score\":0.92"), std::string::npos);
}

TEST(BridgeConversions, IgnoresEmptyDetectionArray) {
    const auto event = nuedc_bridge::detectionArrayToDetectionEvent(
        vision_msgs::msg::Detection2DArray{},
        "mission-1",
        "odom");

    EXPECT_FALSE(event.has_value());
}

TEST(BridgeConversions, SerializesAckEnvelope) {
    const Envelope envelope = nuedc_bridge::buildAckEnvelope(true, "pong");
    const std::string bytes = nuedc_bridge::serializeEnvelope(envelope);

    Envelope parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));
    ASSERT_EQ(parsed.payload_case(), Envelope::kAck);
    EXPECT_TRUE(parsed.ack().success());
    EXPECT_EQ(parsed.ack().message(), "pong");
}

TEST(BridgeConversions, ConvertsTaskPlanToCompatibleJsonWithEscaping) {
    TaskPlanMessage plan;
    plan.set_task_id("mission\"quoted");
    plan.set_task_type("h_problem");
    plan.set_start_waypoint_id("A1");
    plan.set_terminal_waypoint_id("C3");
    plan.set_metadata_json("{\"note\":\"line\\nbreak\"}");
    auto *waypoint = plan.add_waypoints();
    waypoint->set_id("A1");
    waypoint->set_sequence_index(2);
    waypoint->set_x(1.5);
    waypoint->set_y(-2.25);
    waypoint->set_z(3.0);
    waypoint->set_action("scan");
    waypoint->set_payload_json("{\"cell\":\"A1\"}");

    const std::string json = nuedc_bridge::taskPlanToJson(plan);

    EXPECT_NE(json.find("\"message_type\":\"task_plan\""), std::string::npos);
    EXPECT_NE(json.find("\"task_id\":\"mission\\\"quoted\""), std::string::npos);
    EXPECT_NE(json.find("\"metadata_json\":\"{\\\"note\\\":\\\"line\\\\nbreak\\\"}\""), std::string::npos);
    EXPECT_NE(json.find("\"payload_json\":\"{\\\"cell\\\":\\\"A1\\\"}\""), std::string::npos);
}
