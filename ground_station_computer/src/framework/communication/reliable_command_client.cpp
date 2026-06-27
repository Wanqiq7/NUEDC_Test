#include "framework/communication/reliable_command_client.h"

#include "messages.pb.h"

#include <QThread>

#include <utility>

ZmqCommandTransport::ZmqCommandTransport(ZmqCommandClient client)
    : client_(std::move(client)) {}

CommandSendResult ZmqCommandTransport::sendEnvelope(const Envelope &envelope) const {
    return client_.sendEnvelope(envelope);
}

ReliableCommandClient::ReliableCommandClient(const CommandTransport *transport, ReliableCommandPolicy policy)
    : transport_(transport),
      policy_(policy) {
    if (policy_.max_attempts < 1) {
        policy_.max_attempts = 1;
    }
    if (policy_.retry_delay_ms < 0) {
        policy_.retry_delay_ms = 0;
    }
}

CommandSendResult ReliableCommandClient::sendReliable(const Envelope &envelope) {
    if (transport_ == nullptr) {
        const CommandSendResult result{false, "command transport is not configured"};
        markResult(result, false);
        return result;
    }

    status_ = CommandLinkStatus::Connecting;
    CommandSendResult last_result{false, "command was not attempted"};
    for (int attempt = 0; attempt < policy_.max_attempts; ++attempt) {
        last_result = transport_->sendEnvelope(envelope);
        if (last_result.ok) {
            markResult(last_result, envelope.payload_case() == Envelope::kMissionLoad);
            return last_result;
        }
        if (isIdempotentSuccess(envelope, last_result)) {
            last_result.ok = true;
            if (last_result.message.isEmpty() || last_result.message == QString("stale command")) {
                last_result.message = "command already accepted";
            }
            markResult(last_result, envelope.payload_case() == Envelope::kMissionLoad);
            return last_result;
        }
        if (attempt + 1 < policy_.max_attempts && policy_.retry_delay_ms > 0) {
            QThread::msleep(static_cast<unsigned long>(policy_.retry_delay_ms));
        }
    }

    markResult(last_result, false);
    return last_result;
}

CommandSendResult ReliableCommandClient::ping(const QString &task_id) {
    return sendReliable(ZmqCommandClient::buildControlCommandEnvelope(GroundControlCommandType::Ping, task_id));
}

bool ReliableCommandClient::isIdempotentSuccess(const Envelope &envelope, const CommandSendResult &result) const {
    if (result.ok || envelope.sequence() == 0 || result.last_accepted_sequence < envelope.sequence()) {
        return false;
    }

    switch (envelope.payload_case()) {
    case Envelope::kMissionLoad:
        return result.mission_loaded
            && result.task_id == QString::fromStdString(envelope.mission_load().task_id());
    case Envelope::kControlCommand:
        if (!envelope.control_command().task_id().empty()
            && !result.task_id.isEmpty()
            && result.task_id != QString::fromStdString(envelope.control_command().task_id())) {
            return false;
        }
        switch (envelope.control_command().type()) {
        case COMMAND_TYPE_START_MISSION:
            return result.mission_running;
        case COMMAND_TYPE_STOP_MISSION:
            return !result.mission_running;
        default:
            return false;
        }
    default:
        return false;
    }
}

CommandLinkStatus ReliableCommandClient::status() const {
    return status_;
}

QString ReliableCommandClient::lastError() const {
    return last_error_;
}

void ReliableCommandClient::markResult(const CommandSendResult &result, bool mission_synced) {
    if (result.ok) {
        last_error_.clear();
        status_ = mission_synced ? CommandLinkStatus::MissionSynced : CommandLinkStatus::Connected;
        return;
    }

    last_error_ = result.message;
    status_ = CommandLinkStatus::Offline;
}
