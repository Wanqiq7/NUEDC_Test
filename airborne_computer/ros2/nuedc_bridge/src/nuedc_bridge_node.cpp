#include "nuedc_bridge/bridge_conversions.h"

#include "messages.pb.h"

#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <zmq.hpp>

namespace {

bool writeBytesToFile(const std::string &path, const std::string &bytes, std::string *error_message) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "failed to open " + path;
        }
        return false;
    }
    output << bytes;
    if (!output.good()) {
        if (error_message != nullptr) {
            *error_message = "failed to write " + path;
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

} // namespace

class NuedcBridgeNode final : public rclcpp::Node {
public:
    NuedcBridgeNode()
        : Node("nuedc_bridge_node"),
          context_(1),
          pub_socket_(context_, zmq::socket_type::pub) {
        telemetry_endpoint_ = declare_parameter<std::string>("telemetry_endpoint", "tcp://0.0.0.0:5557");
        command_endpoint_ = declare_parameter<std::string>("command_endpoint", "tcp://0.0.0.0:5558");
        mission_plan_path_ = declare_parameter<std::string>("mission_plan_path", "runtime/active_mission_plan.json");
        task_id_ = declare_parameter<std::string>("task_id", "ros2-bridge");
        odometry_topic_ = declare_parameter<std::string>("odometry_topic", "Odometry");
        detections_topic_ = declare_parameter<std::string>("detections_topic", "detections");

        pub_socket_.bind(telemetry_endpoint_);

        odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
            odometry_topic_,
            rclcpp::SensorDataQoS(),
            [this](const nav_msgs::msg::Odometry::SharedPtr message) {
                handleOdometry(*message);
            });
        detection_subscription_ = create_subscription<vision_msgs::msg::Detection2DArray>(
            detections_topic_,
            rclcpp::SensorDataQoS(),
            [this](const vision_msgs::msg::Detection2DArray::SharedPtr message) {
                handleDetections(*message);
            });

        command_thread_ = std::thread([this]() {
            commandLoop();
        });

        RCLCPP_INFO(
            get_logger(),
            "NUEDC bridge started: PUB %s, REP %s",
            telemetry_endpoint_.c_str(),
            command_endpoint_.c_str());
    }

    ~NuedcBridgeNode() override {
        stop_requested_.store(true);
        if (command_thread_.joinable()) {
            command_thread_.join();
        }
        pub_socket_.close();
        context_.close();
    }

private:
    void handleOdometry(const nav_msgs::msg::Odometry &message) {
        const uint32_t event_index = odometry_count_.fetch_add(1);
        publishEvent(nuedc_bridge::odometryToTelemetryEvent(message, activeTaskId(), event_index));
    }

    void handleDetections(const vision_msgs::msg::Detection2DArray &message) {
        const uint32_t event_index = detection_count_.fetch_add(1);
        const auto event = nuedc_bridge::detectionArrayToDetectionEvent(message, activeTaskId(), "odom", event_index);
        if (event.has_value()) {
            publishEvent(event.value());
        }
    }

    std::string activeTaskId() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return task_id_;
    }

    void publishEvent(const TaskEventMessage &event) {
        const uint64_t sequence = publish_sequence_.fetch_add(1);
        const Envelope envelope = nuedc_bridge::buildTaskEventEnvelope(sequence, event);
        const std::string bytes = nuedc_bridge::serializeEnvelope(envelope);
        pub_socket_.send(zmq::buffer(bytes), zmq::send_flags::none);
    }

    void commandLoop() {
        zmq::socket_t rep_socket(context_, zmq::socket_type::rep);
        rep_socket.set(zmq::sockopt::linger, 0);
        rep_socket.set(zmq::sockopt::rcvtimeo, 100);
        rep_socket.bind(command_endpoint_);

        while (!stop_requested_.load()) {
            zmq::message_t request;
            const auto received = rep_socket.recv(request, zmq::recv_flags::none);
            if (!received.has_value()) {
                continue;
            }

            Envelope envelope;
            const bool parsed = envelope.ParseFromArray(request.data(), static_cast<int>(request.size()));
            Envelope ack;
            if (!parsed) {
                ack = buildCurrentStateAck(false, "invalid protobuf");
            } else {
                ack = handleCommand(envelope);
            }
            const std::string reply = nuedc_bridge::serializeEnvelope(ack);
            rep_socket.send(zmq::buffer(reply), zmq::send_flags::none);
        }
        rep_socket.close();
    }

    Envelope buildCurrentStateAck(bool success, const std::string &message) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return buildAckLocked(success, message);
    }

    Envelope buildAckLocked(bool success, const std::string &message) const {
        return nuedc_bridge::buildAckEnvelope(
            success,
            message,
            task_id_,
            mission_loaded_,
            mission_running_,
            last_accepted_sequence_);
    }

    bool isStaleSequenceLocked(uint64_t sequence) const {
        return sequence != 0 && sequence <= last_accepted_sequence_;
    }

    void acceptSequenceLocked(uint64_t sequence) {
        if (sequence != 0) {
            last_accepted_sequence_ = sequence;
        }
    }

    Envelope handleCommand(const Envelope &envelope) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        switch (envelope.payload_case()) {
        case Envelope::kMissionLoad: {
            if (isStaleSequenceLocked(envelope.sequence())) {
                return buildAckLocked(false, "stale command");
            }
            std::string error;
            if (!writeBytesToFile(mission_plan_path_, nuedc_bridge::taskPlanToJson(envelope.mission_load()), &error)) {
                return buildAckLocked(false, error);
            }
            task_id_ = envelope.mission_load().task_id();
            mission_loaded_ = true;
            mission_running_ = false;
            acceptSequenceLocked(envelope.sequence());
            return buildAckLocked(true, "task plan stored");
        }
        case Envelope::kControlCommand:
            switch (envelope.control_command().type()) {
            case COMMAND_TYPE_START_MISSION:
                if (isStaleSequenceLocked(envelope.sequence())) {
                    return buildAckLocked(false, "stale command");
                }
                if (!mission_loaded_) {
                    return buildAckLocked(false, "mission is not loaded");
                }
                mission_running_ = true;
                acceptSequenceLocked(envelope.sequence());
                return buildAckLocked(true, "start accepted");
            case COMMAND_TYPE_STOP_MISSION:
                if (isStaleSequenceLocked(envelope.sequence())) {
                    return buildAckLocked(false, "stale command");
                }
                mission_running_ = false;
                acceptSequenceLocked(envelope.sequence());
                return buildAckLocked(true, "stop accepted");
            case COMMAND_TYPE_PING:
                return buildAckLocked(true, "pong");
            default:
                return buildAckLocked(false, "unsupported command");
            }
        default:
            return buildAckLocked(false, "unsupported payload");
        }
    }

    std::string telemetry_endpoint_;
    std::string command_endpoint_;
    std::string mission_plan_path_;
    std::string task_id_;
    std::string odometry_topic_;
    std::string detections_topic_;

    mutable std::mutex state_mutex_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint64_t> publish_sequence_{1};
    std::atomic<uint32_t> odometry_count_{0};
    std::atomic<uint32_t> detection_count_{0};
    bool mission_loaded_ = false;
    bool mission_running_ = false;
    uint64_t last_accepted_sequence_ = 0;
    zmq::context_t context_;
    zmq::socket_t pub_socket_;
    std::thread command_thread_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
    rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr detection_subscription_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<NuedcBridgeNode>());
    rclcpp::shutdown();
    return 0;
}
