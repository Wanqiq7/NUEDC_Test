#include "framework/communication/zmq_command_client.h"

#include "messages.pb.h"

#include <QByteArray>
#include <QDateTime>

#include <atomic>

#include <zmq.hpp>

namespace {

constexpr int kSequenceCounterBits = 20;

quint64 initialCommandSequence() {
    const qint64 current_ms = QDateTime::currentMSecsSinceEpoch();
    if (current_ms <= 0) {
        return 0;
    }
    return static_cast<quint64>(current_ms) << kSequenceCounterBits;
}

std::atomic<quint64> global_command_sequence{initialCommandSequence()};

CommandType toProtoCommandType(GroundControlCommandType command_type) {
    switch (command_type) {
    case GroundControlCommandType::StartMission:
        return CommandType::COMMAND_TYPE_START_MISSION;
    case GroundControlCommandType::StopMission:
        return CommandType::COMMAND_TYPE_STOP_MISSION;
    case GroundControlCommandType::Ping:
        return CommandType::COMMAND_TYPE_PING;
    case GroundControlCommandType::ArmTargeting:
        return CommandType::COMMAND_TYPE_ARM_TARGETING;
    case GroundControlCommandType::ResetTargeting:
        return CommandType::COMMAND_TYPE_RESET_TARGETING;
    }
    return CommandType::COMMAND_TYPE_UNSPECIFIED;
}

CommandSendResult sendEnvelopeToEndpoint(const Envelope &envelope, const QString &endpoint) {
    try {
        std::string bytes;
        envelope.SerializeToString(&bytes);

        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::req);
        socket.set(zmq::sockopt::linger, 0);
        socket.set(zmq::sockopt::rcvtimeo, 1500);
        socket.set(zmq::sockopt::sndtimeo, 1500);
        socket.connect(endpoint.toStdString());
        socket.send(zmq::buffer(bytes), zmq::send_flags::none);

        zmq::message_t reply;
        const auto received = socket.recv(reply, zmq::recv_flags::none);
        if (!received) {
            return CommandSendResult{false, "command ack timed out"};
        }

        return EnvelopeCodec::parseAck(
            QByteArray(static_cast<const char *>(reply.data()), static_cast<int>(reply.size())));
    } catch (const std::exception &exception) {
        return CommandSendResult{false, QString::fromUtf8(exception.what())};
    }
}

} // namespace

ZmqCommandClient::ZmqCommandClient(QString endpoint)
    : endpoint_(std::move(endpoint)) {}

quint64 ZmqCommandClient::nextCommandSequence() {
    return global_command_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

CommandSendResult ZmqCommandClient::parseAck(const QByteArray &payload) {
    return EnvelopeCodec::parseAck(payload);
}

Envelope ZmqCommandClient::buildControlCommandEnvelope(
    GroundControlCommandType command_type,
    const QString &task_id) {
    return buildControlCommandEnvelope(ZmqCommandClient::nextCommandSequence(), command_type, task_id);
}

Envelope ZmqCommandClient::buildControlCommandEnvelope(
    quint64 sequence,
    GroundControlCommandType command_type,
    const QString &task_id) {
    Envelope envelope;
    envelope.set_sequence(sequence);
    auto *payload = envelope.mutable_control_command();
    payload->set_type(toProtoCommandType(command_type));
    payload->set_task_id(task_id.toStdString());
    return envelope;
}

CommandSendResult ZmqCommandClient::sendEnvelope(const Envelope &envelope) const {
    return sendEnvelopeToEndpoint(envelope, endpoint_);
}

CommandSendResult ZmqCommandClient::sendControlCommand(
    GroundControlCommandType command_type,
    const QString &task_id) const {
    return sendEnvelope(buildControlCommandEnvelope(command_type, task_id));
}
