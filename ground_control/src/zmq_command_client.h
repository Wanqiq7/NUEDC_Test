#pragma once

#include <QString>

#include "mission_plan_bridge.h"

struct CommandSendResult {
    bool ok = false;
    QString message;
};

enum class GroundControlCommandType {
    StartMission,
    StopMission,
    Ping,
};

class ZmqCommandClient {
public:
    explicit ZmqCommandClient(QString endpoint = "tcp://127.0.0.1:5558");

    static CommandSendResult parseAck(const QByteArray &payload);
    CommandSendResult sendMissionPlan(const MissionPlanData &plan) const;
    CommandSendResult sendControlCommand(GroundControlCommandType command_type, const QString &case_id = {}) const;

private:
    QString endpoint_;
};
