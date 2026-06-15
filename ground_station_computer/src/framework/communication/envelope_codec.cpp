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
    return CommandSendResult{ack.success(), QString::fromStdString(ack.message())};
}
