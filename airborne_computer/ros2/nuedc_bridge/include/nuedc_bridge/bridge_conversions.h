#pragma once

#include "messages.pb.h"

#include <nav_msgs/msg/odometry.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>

#include <optional>
#include <string>

namespace nuedc_bridge {

TaskEventMessage odometryToTelemetryEvent(
    const nav_msgs::msg::Odometry &message,
    const std::string &task_id,
    uint32_t sequence_index);

std::optional<TaskEventMessage> detectionArrayToDetectionEvent(
    const vision_msgs::msg::Detection2DArray &message,
    const std::string &task_id,
    const std::string &waypoint_id,
    uint32_t sequence_index);

Envelope buildTaskEventEnvelope(uint64_t sequence, const TaskEventMessage &event);
Envelope buildAckEnvelope(bool success, const std::string &message);
Envelope buildAckEnvelope(
    bool success,
    const std::string &message,
    const std::string &task_id,
    bool mission_loaded,
    bool mission_running,
    uint64_t last_accepted_sequence);

std::string taskPlanToJson(const TaskPlanMessage &plan);
std::string serializeEnvelope(const Envelope &envelope);

} // namespace nuedc_bridge
