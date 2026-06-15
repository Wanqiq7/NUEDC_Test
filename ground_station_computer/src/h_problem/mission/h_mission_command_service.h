#pragma once

#include <QString>

#include "framework/communication/envelope_codec.h"
#include "h_problem/mission/h_route_planner_bridge.h"
#include "framework/communication/zmq_command_client.h"

class MissionCommandService {
public:
    explicit MissionCommandService(ZmqCommandClient client = ZmqCommandClient());

    CommandSendResult sendMissionPlan(const MissionPlanData &plan) const;
    CommandSendResult sendControlCommand(GroundControlCommandType command_type, const QString &task_id = {}) const;

private:
    ZmqCommandClient client_;
};
