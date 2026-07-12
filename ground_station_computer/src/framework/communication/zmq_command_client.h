#pragma once

#include <QtGlobal>
#include <QString>

#include "framework/communication/envelope_codec.h"

class Envelope;

enum class GroundControlCommandType {
    StartMission,
    StopMission,
    Ping,
    ArmTargeting,
    DisarmTargeting,
};

class ZmqCommandClient {
public:
    explicit ZmqCommandClient(QString endpoint = "tcp://127.0.0.1:5558");

    static quint64 nextCommandSequence();
    static Envelope buildControlCommandEnvelope(GroundControlCommandType command_type, const QString &task_id = {});
    static Envelope buildControlCommandEnvelope(quint64 sequence, GroundControlCommandType command_type, const QString &task_id = {});
    static CommandSendResult parseAck(const QByteArray &payload);
    CommandSendResult sendEnvelope(const Envelope &envelope) const;
    CommandSendResult sendControlCommand(GroundControlCommandType command_type, const QString &task_id = {}) const;

private:
    QString endpoint_;
};
