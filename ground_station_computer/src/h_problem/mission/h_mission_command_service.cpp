#include "h_problem/mission/h_mission_command_service.h"

#include "messages.pb.h"
#include "h_problem/mission/h_mission_load_adapter.h"

#include <utility>

MissionCommandService::MissionCommandService(ZmqCommandClient client)
    : client_(std::move(client)),
      owned_transport_(std::make_unique<ZmqCommandTransport>(client_)),
      active_transport_(owned_transport_.get()),
      reliable_client_(active_transport_) {}

MissionCommandService::MissionCommandService(const CommandTransport *transport)
    : active_transport_(transport),
      reliable_client_(active_transport_) {}

MissionCommandService::MissionCommandService(const MissionCommandService &other)
    : client_(other.client_),
      owned_transport_(std::make_unique<ZmqCommandTransport>(client_)),
      active_transport_(owned_transport_.get()),
      reliable_client_(active_transport_) {}

MissionCommandService &MissionCommandService::operator=(const MissionCommandService &other) {
    if (this == &other) {
        return *this;
    }

    client_ = other.client_;
    owned_transport_ = std::make_unique<ZmqCommandTransport>(client_);
    active_transport_ = owned_transport_.get();
    reliable_client_ = ReliableCommandClient(active_transport_);
    return *this;
}

CommandSendResult MissionCommandService::sendMissionPlan(const MissionPlanData &plan) {
    return reliable_client_.sendReliable(MissionLoadAdapter::buildMissionLoadEnvelope(ZmqCommandClient::nextCommandSequence(), plan));
}

CommandSendResult MissionCommandService::sendControlCommand(
    GroundControlCommandType command_type,
    const QString &task_id) {
    return reliable_client_.sendReliable(ZmqCommandClient::buildControlCommandEnvelope(command_type, task_id));
}

CommandLinkStatus MissionCommandService::linkStatus() const {
    return reliable_client_.status();
}
