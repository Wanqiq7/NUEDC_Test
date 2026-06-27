#pragma once

#include "framework/communication/envelope_codec.h"
#include "framework/communication/zmq_command_client.h"

#include <QString>

class Envelope;

enum class CommandLinkStatus {
    Offline,
    Connecting,
    Connected,
    MissionSynced,
};

struct ReliableCommandPolicy {
    int max_attempts = 3;
    int retry_delay_ms = 120;
};

class CommandTransport {
public:
    virtual ~CommandTransport() = default;

    virtual CommandSendResult sendEnvelope(const Envelope &envelope) const = 0;
};

class ZmqCommandTransport final : public CommandTransport {
public:
    explicit ZmqCommandTransport(ZmqCommandClient client);

    CommandSendResult sendEnvelope(const Envelope &envelope) const override;

private:
    ZmqCommandClient client_;
};

class ReliableCommandClient {
public:
    ReliableCommandClient(const CommandTransport *transport, ReliableCommandPolicy policy = {});

    CommandSendResult sendReliable(const Envelope &envelope);
    CommandSendResult ping(const QString &task_id = {});
    CommandLinkStatus status() const;
    QString lastError() const;

private:
    bool isIdempotentSuccess(const Envelope &envelope, const CommandSendResult &result) const;
    void markResult(const CommandSendResult &result, bool mission_synced);

    const CommandTransport *transport_ = nullptr;
    ReliableCommandPolicy policy_;
    CommandLinkStatus status_ = CommandLinkStatus::Offline;
    QString last_error_;
};
