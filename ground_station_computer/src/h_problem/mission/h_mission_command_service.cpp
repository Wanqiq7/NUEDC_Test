#include "h_problem/mission/h_mission_command_service.h"

#include "messages.pb.h"
#include "h_problem/mission/h_mission_load_adapter.h"

#include <utility>

MissionCommandService::MissionCommandService(ZmqCommandClient client)
    : client_(std::move(client)) {}

CommandSendResult MissionCommandService::sendMissionPlan(const MissionPlanData &plan) const {
    return client_.sendEnvelope(MissionLoadAdapter::buildMissionLoadEnvelope(plan));
}

CommandSendResult MissionCommandService::sendControlCommand(
    GroundControlCommandType command_type,
    const QString &task_id) const {
    return client_.sendControlCommand(command_type, task_id);
}
