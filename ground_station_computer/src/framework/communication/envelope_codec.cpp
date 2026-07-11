#include "framework/communication/envelope_codec.h"

#include "messages.pb.h"

CommandSendResult EnvelopeCodec::parseAck(const QByteArray &payload) {
    Envelope envelope;
    if (!envelope.ParseFromArray(payload.constData(), static_cast<int>(payload.size()))) {
        return CommandSendResult{false, "failed to parse ack envelope"};
    }

    if (envelope.payload_case() != Envelope::kAck) {
        return CommandSendResult{false, "reply did not contain ack"};
    }

    const auto &ack = envelope.ack();
    CommandSendResult result;
    result.ok = ack.success();
    result.message = QString::fromStdString(ack.message());
    result.task_id = QString::fromStdString(ack.task_id());
    result.mission_loaded = ack.mission_loaded();
    result.mission_running = ack.mission_running();
    result.last_accepted_sequence = ack.last_accepted_sequence();
    result.vision_armed = ack.vision_armed();
    return result;
}
