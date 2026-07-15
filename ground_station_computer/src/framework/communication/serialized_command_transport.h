#pragma once

#include "framework/communication/reliable_command_client.h"

#include <memory>
#include <mutex>

class SerializedCommandTransport final : public CommandTransport {
public:
    explicit SerializedCommandTransport(std::unique_ptr<CommandTransport> inner);

    CommandSendResult sendEnvelope(const Envelope &envelope) const override;

private:
    std::unique_ptr<CommandTransport> inner_;
    mutable std::mutex mutex_;
};
