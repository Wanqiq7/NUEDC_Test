#include "framework/communication/serialized_command_transport.h"

#include <utility>

SerializedCommandTransport::SerializedCommandTransport(std::unique_ptr<CommandTransport> inner)
    : inner_(std::move(inner)) {}

CommandSendResult SerializedCommandTransport::sendEnvelope(const Envelope &envelope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inner_ == nullptr
        ? CommandSendResult{false, "command transport is not configured"}
        : inner_->sendEnvelope(envelope);
}
