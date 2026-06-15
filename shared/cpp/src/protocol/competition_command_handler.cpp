#include "competition_core/protocol/command_handler.h"

#include "competition_core/mission/task_plan_store.h"
#include "competition_core/protocol/envelope_codec.h"

namespace competition {

AckResult handleEnvelopeCommand(const Envelope &envelope, const QString &output_path, CommandState *state) {
    switch (envelope.payload_case()) {
    case Envelope::kMissionLoad: {
        QString parse_error;
        const auto plan = taskPlanFromMessage(envelope.mission_load(), &parse_error);
        if (!plan.has_value()) {
            return {false, parse_error};
        }
        QString error;
        if (!storeTaskPlan(plan.value(), output_path, &error)) {
            return {false, error};
        }
        if (state != nullptr) {
            state->mission_loaded = true;
            state->active_task_plan = plan.value();
        }
        return {true, "task plan stored"};
    }
    case Envelope::kControlCommand:
        switch (envelope.control_command().type()) {
        case COMMAND_TYPE_START_MISSION:
            if (state != nullptr) {
                state->stop_requested = false;
                state->start_requested = true;
            }
            return {true, "start accepted"};
        case COMMAND_TYPE_STOP_MISSION:
            if (state != nullptr) {
                state->stop_requested = true;
            }
            return {true, "stop accepted"};
        case COMMAND_TYPE_PING:
            return {true, "pong"};
        default:
            return {false, "unsupported command"};
        }
    default:
        return {false, "unsupported payload"};
    }
}

AckResult handleCommandBytes(const QByteArray &payload, const QString &output_path, CommandState *state) {
    Envelope envelope;
    if (!envelope.ParseFromArray(payload.constData(), static_cast<int>(payload.size()))) {
        return {false, "invalid protobuf"};
    }
    return handleEnvelopeCommand(envelope, output_path, state);
}

} // namespace competition
