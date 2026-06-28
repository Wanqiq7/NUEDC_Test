#include "competition_core/protocol/command_handler.h"

#include "competition_core/mission/task_plan_store.h"
#include "competition_core/protocol/envelope_codec.h"

#include <QMutexLocker>

namespace competition {

namespace {

AckResult staleCommandResult(const CommandState *state) {
    AckResult result{false, "stale command"};
    if (state != nullptr) {
        result.task_id = state->activeTaskPlan().task_id;
        result.mission_loaded = state->isMissionLoaded();
        result.mission_running = state->isStartRequested() && !state->isStopRequested();
        result.last_accepted_sequence = state->lastAcceptedSequence();
    }
    return result;
}

bool rejectStaleSequence(const Envelope &envelope, CommandState *state, AckResult *result) {
    if (state == nullptr || !state->isStaleSequence(envelope.sequence())) {
        return false;
    }
    if (result != nullptr) {
        *result = staleCommandResult(state);
    }
    return true;
}

void acceptEnvelopeSequence(const Envelope &envelope, CommandState *state) {
    if (state != nullptr) {
        state->acceptSequence(envelope.sequence());
    }
}

} // namespace

MissionCommandStateMachine::MissionCommandStateMachine(CommandState *state)
    : state_(state) {}

AckResult MissionCommandStateMachine::apply(const Envelope &envelope) {
    QMutexLocker<QMutex> command_lock(state_ != nullptr ? state_->commandMutex() : nullptr);

    if (envelope.payload_case() != Envelope::kControlCommand) {
        return {false, "unsupported payload"};
    }

    switch (envelope.control_command().type()) {
    case COMMAND_TYPE_START_MISSION: {
        AckResult stale_result;
        if (rejectStaleSequence(envelope, state_, &stale_result)) {
            return stale_result;
        }
        if (state_ != nullptr) {
            state_->requestStart();
        }
        acceptEnvelopeSequence(envelope, state_);
        return {true, "start accepted"};
    }
    case COMMAND_TYPE_STOP_MISSION: {
        AckResult stale_result;
        if (rejectStaleSequence(envelope, state_, &stale_result)) {
            return stale_result;
        }
        if (state_ != nullptr) {
            state_->requestStop();
        }
        acceptEnvelopeSequence(envelope, state_);
        return {true, "stop accepted"};
    }
    case COMMAND_TYPE_PING:
        return {true, "pong"};
    default:
        return {false, "unsupported command"};
    }
}

AckResult applyCommandEnvelope(const Envelope &envelope, CommandState *state) {
    MissionCommandStateMachine state_machine(state);
    return state_machine.apply(envelope);
}

bool isCommandAlreadyAccepted(const Envelope &envelope, const AckResult &result) {
    if (result.success || envelope.sequence() == 0 || result.last_accepted_sequence < envelope.sequence()) {
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

AckResult handleEnvelopeCommand(const Envelope &envelope, const QString &output_path, CommandState *state) {
    QMutexLocker<QMutex> command_lock(state != nullptr ? state->commandMutex() : nullptr);

    switch (envelope.payload_case()) {
    case Envelope::kMissionLoad: {
        AckResult stale_result;
        if (rejectStaleSequence(envelope, state, &stale_result)) {
            return stale_result;
        }

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
            state->setActiveTaskPlan(plan.value());
            state->setMissionLoaded(true);
        }
        acceptEnvelopeSequence(envelope, state);
        return {true, "task plan stored"};
    }
    case Envelope::kControlCommand: {
        command_lock.unlock();
        return applyCommandEnvelope(envelope, state);
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
