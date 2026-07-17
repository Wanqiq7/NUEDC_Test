#include "framework/communication/reliable_command_client.h"

#include "competition_core/protocol/command_handler.h"
#include "messages.pb.h"

#include <QThread>

#include <utility>

namespace {

competition::AckResult toAckResult(const CommandSendResult &result) {
    return competition::AckResult{
        result.ok,
        result.message,
        result.task_id,
        result.mission_loaded,
        result.mission_running,
        result.last_accepted_sequence,
        result.vision_armed,
    };
}

} // namespace

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
        if (competition::isCommandAlreadyAccepted(envelope, toAckResult(last_result))) {
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
    return sendReliable(ZmqCommandClient::buildControlCommandEnvelope(
        0,
        GroundControlCommandType::Ping,
        task_id));
}

CommandLinkStatus ReliableCommandClient::status() const {
    return status_;
}

QString ReliableCommandClient::lastError() const {
    return last_error_;
}

QString ReliableCommandClient::operatorStatusText(const QString &action_text, const CommandSendResult &result) {
    if (result.ok && result.message == QStringLiteral("command already accepted")) {
        return QString("状态: %1已通过幂等重试确认到达 | seq %2")
            .arg(action_text, QString::number(result.last_accepted_sequence));
    }
    if (result.ok) {
        return QString("状态: 已发送%1").arg(action_text);
    }
    return QString("错误: %1失败 | %2").arg(action_text, result.message);
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
