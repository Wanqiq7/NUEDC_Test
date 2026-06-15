#pragma once

#include <QString>

#include "framework/communication/envelope_codec.h"
#include "framework/communication/reliable_command_client.h"
#include "h_problem/mission/h_route_planner_bridge.h"
#include "framework/communication/zmq_command_client.h"

#include <memory>

class MissionCommandService {
public:
    explicit MissionCommandService(ZmqCommandClient client = ZmqCommandClient());
    explicit MissionCommandService(const CommandTransport *transport);
    MissionCommandService(const MissionCommandService &other);
    MissionCommandService &operator=(const MissionCommandService &other);

    CommandSendResult sendMissionPlan(const MissionPlanData &plan);
    CommandSendResult sendControlCommand(GroundControlCommandType command_type, const QString &task_id = {});
    CommandLinkStatus linkStatus() const;

private:
    ZmqCommandClient client_;
    std::unique_ptr<ZmqCommandTransport> owned_transport_;
    const CommandTransport *active_transport_ = nullptr;
    ReliableCommandClient reliable_client_;
};
