#include "nuedc_bridge/bridge_conversions.h"

#include <chrono>
#include <sstream>

namespace nuedc_bridge {

namespace {

int64_t nowMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string jsonNumber(double value) {
    std::ostringstream stream;
    stream.precision(12);
    stream << value;
    return stream.str();
}

std::string escapeJson(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

} // namespace

TaskEventMessage odometryToTelemetryEvent(
    const nav_msgs::msg::Odometry &message,
    const std::string &task_id,
    uint32_t sequence_index) {
    TaskEventMessage event;
    event.set_task_id(task_id);
    event.set_event_type("telemetry");
    event.set_sequence_index(sequence_index);
    event.set_waypoint_id("odom");

    const auto &position = message.pose.pose.position;
    const auto &linear = message.twist.twist.linear;
    std::ostringstream payload;
    payload << "{"
            << "\"current_cell\":\"odom\","
            << "\"visited_cells\":" << (sequence_index + 1) << ","
            << "\"frame_id\":\"" << escapeJson(message.header.frame_id) << "\","
            << "\"position_x\":" << jsonNumber(position.x) << ","
            << "\"position_y\":" << jsonNumber(position.y) << ","
            << "\"position_z\":" << jsonNumber(position.z) << ","
            << "\"linear_x\":" << jsonNumber(linear.x) << ","
            << "\"linear_y\":" << jsonNumber(linear.y) << ","
            << "\"linear_z\":" << jsonNumber(linear.z)
            << "}";
    event.set_payload_json(payload.str());
    return event;
}

std::optional<TaskEventMessage> detectionArrayToDetectionEvent(
    const vision_msgs::msg::Detection2DArray &message,
    const std::string &task_id,
    const std::string &waypoint_id) {
    if (message.detections.empty()) {
        return std::nullopt;
    }

    const auto &detection = message.detections.front();
    std::string animal_name = detection.id;
    double score = 0.0;
    if (!detection.results.empty()) {
        animal_name = detection.results.front().hypothesis.class_id;
        score = detection.results.front().hypothesis.score;
    }
    if (animal_name.empty()) {
        animal_name = "unknown";
    }

    TaskEventMessage event;
    event.set_task_id(task_id);
    event.set_event_type("detection");
    event.set_sequence_index(0);
    event.set_waypoint_id(waypoint_id);

    std::ostringstream payload;
    payload << "{"
            << "\"cell_code\":\"" << escapeJson(waypoint_id) << "\","
            << "\"animal_name\":\"" << escapeJson(animal_name) << "\","
            << "\"count\":" << message.detections.size() << ","
            << "\"score\":" << jsonNumber(score) << ","
            << "\"bbox_center_x\":" << jsonNumber(detection.bbox.center.position.x) << ","
            << "\"bbox_center_y\":" << jsonNumber(detection.bbox.center.position.y)
            << "}";
    event.set_payload_json(payload.str());
    return event;
}

Envelope buildTaskEventEnvelope(uint64_t sequence, const TaskEventMessage &event) {
    Envelope envelope;
    envelope.set_sequence(sequence);
    envelope.set_timestamp_ms(nowMs());
    *envelope.mutable_task_event() = event;
    return envelope;
}

Envelope buildAckEnvelope(bool success, const std::string &message) {
    Envelope envelope;
    envelope.set_sequence(0);
    auto *ack = envelope.mutable_ack();
    ack->set_success(success);
    ack->set_message(message);
    return envelope;
}

std::string taskPlanToJson(const TaskPlanMessage &plan) {
    std::ostringstream json;
    json << "{"
         << "\"message_type\":\"task_plan\","
         << "\"task_id\":\"" << escapeJson(plan.task_id()) << "\","
         << "\"task_type\":\"" << escapeJson(plan.task_type()) << "\","
         << "\"start_waypoint_id\":\"" << escapeJson(plan.start_waypoint_id()) << "\","
         << "\"terminal_waypoint_id\":\"" << escapeJson(plan.terminal_waypoint_id()) << "\","
         << "\"waypoints\":[";

    for (int index = 0; index < plan.waypoints_size(); ++index) {
        const auto &waypoint = plan.waypoints(index);
        if (index > 0) {
            json << ",";
        }
        json << "{"
             << "\"id\":\"" << escapeJson(waypoint.id()) << "\","
             << "\"sequence_index\":" << waypoint.sequence_index() << ","
             << "\"x\":" << jsonNumber(waypoint.x()) << ","
             << "\"y\":" << jsonNumber(waypoint.y()) << ","
             << "\"z\":" << jsonNumber(waypoint.z()) << ","
             << "\"action\":\"" << escapeJson(waypoint.action()) << "\","
             << "\"payload_json\":\"" << escapeJson(waypoint.payload_json()) << "\""
             << "}";
    }

    json << "],"
         << "\"metadata_json\":\"" << escapeJson(plan.metadata_json()) << "\""
         << "}";
    return json.str();
}

std::string serializeEnvelope(const Envelope &envelope) {
    std::string bytes;
    envelope.SerializeToString(&bytes);
    return bytes;
}

} // namespace nuedc_bridge
