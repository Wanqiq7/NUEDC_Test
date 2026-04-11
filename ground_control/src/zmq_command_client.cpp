#include "zmq_command_client.h"

#include "messages.pb.h"

#include <QByteArray>

#include <zmq.hpp>

namespace {

CommandType toProtoCommandType(GroundControlCommandType command_type) {
    switch (command_type) {
    case GroundControlCommandType::StartMission:
        return CommandType::COMMAND_TYPE_START_MISSION;
    case GroundControlCommandType::StopMission:
        return CommandType::COMMAND_TYPE_STOP_MISSION;
    case GroundControlCommandType::Ping:
        return CommandType::COMMAND_TYPE_PING;
    }
    return CommandType::COMMAND_TYPE_UNSPECIFIED;
}

CommandSendResult sendEnvelope(const Envelope &envelope, const QString &endpoint) {
    try {
        std::string bytes;
        envelope.SerializeToString(&bytes);

        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::req);
        socket.set(zmq::sockopt::rcvtimeo, 1500);
        socket.set(zmq::sockopt::sndtimeo, 1500);
        socket.connect(endpoint.toStdString());
        socket.send(zmq::buffer(bytes), zmq::send_flags::none);

        zmq::message_t reply;
        const auto received = socket.recv(reply, zmq::recv_flags::none);
        if (!received) {
            return CommandSendResult{false, "command ack timed out"};
        }

        return ZmqCommandClient::parseAck(
            QByteArray(static_cast<const char *>(reply.data()), static_cast<int>(reply.size())));
    } catch (const std::exception &exception) {
        return CommandSendResult{false, QString::fromUtf8(exception.what())};
    }
}

} // namespace

ZmqCommandClient::ZmqCommandClient(QString endpoint)
    : endpoint_(std::move(endpoint)) {}

CommandSendResult ZmqCommandClient::parseAck(const QByteArray &payload) {
    Envelope envelope;
    if (!envelope.ParseFromArray(payload.constData(), static_cast<int>(payload.size()))) {
        return CommandSendResult{false, "failed to parse ack envelope"};
    }

    if (envelope.payload_case() != Envelope::kAck) {
        return CommandSendResult{false, "reply did not contain ack"};
    }

    const auto &ack = envelope.ack();
    return CommandSendResult{ack.success(), QString::fromStdString(ack.message())};
}

CommandSendResult ZmqCommandClient::sendMissionPlan(const MissionPlanData &plan) const {
    Envelope envelope;
    envelope.set_sequence(0);
    auto *payload = envelope.mutable_mission_load();
    payload->set_case_id(plan.case_id.toStdString());
    payload->set_start_cell(plan.start_cell.toStdString());
    for (const QString &cell_code : plan.no_fly_cells) {
        payload->add_no_fly_cells(cell_code.toStdString());
    }
    for (const QString &cell_code : plan.route) {
        payload->add_route(cell_code.toStdString());
    }
    payload->set_terminal_cell(plan.terminal_cell.toStdString());
    payload->set_landing_enabled(plan.landing_enabled);
    payload->set_descent_angle_deg(static_cast<float>(plan.descent_angle_deg.value_or(0.0)));
    payload->set_takeoff_anchor_x_cm(static_cast<float>(plan.takeoff_anchor_x_cm.value_or(0.0)));
    payload->set_takeoff_anchor_y_cm(static_cast<float>(plan.takeoff_anchor_y_cm.value_or(0.0)));
    return sendEnvelope(envelope, endpoint_);
}

CommandSendResult ZmqCommandClient::sendControlCommand(
    GroundControlCommandType command_type,
    const QString &case_id) const {
    Envelope envelope;
    envelope.set_sequence(0);
    auto *payload = envelope.mutable_control_command();
    payload->set_type(toProtoCommandType(command_type));
    payload->set_case_id(case_id.toStdString());
    return sendEnvelope(envelope, endpoint_);
}
