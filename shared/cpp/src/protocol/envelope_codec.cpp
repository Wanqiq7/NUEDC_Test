#include "competition_core/protocol/envelope_codec.h"

#include <QDateTime>

namespace competition {

namespace {

Envelope newEnvelope(quint64 sequence) {
    Envelope envelope;
    envelope.set_sequence(sequence);
    envelope.set_timestamp_ms(QDateTime::currentMSecsSinceEpoch());
    return envelope;
}

} // namespace

TaskPlanMessage taskPlanToMessage(const TaskPlan &plan) {
    TaskPlanMessage message;
    message.set_task_id(plan.task_id.toStdString());
    message.set_task_type(plan.task_type.toStdString());
    message.set_start_waypoint_id(plan.start_waypoint_id.toStdString());
    message.set_terminal_waypoint_id(plan.terminal_waypoint_id.toStdString());
    message.set_metadata_json(plan.metadata_json.toStdString());
    for (const TaskWaypoint &waypoint : plan.waypoints) {
        auto *payload = message.add_waypoints();
        payload->set_id(waypoint.id.toStdString());
        payload->set_sequence_index(waypoint.sequence_index);
        payload->set_x(waypoint.x);
        payload->set_y(waypoint.y);
        payload->set_z(waypoint.z);
        payload->set_action(waypoint.action.toStdString());
        payload->set_payload_json(waypoint.payload_json.toStdString());
    }
    return message;
}

TaskEventMessage taskEventToMessage(const TaskEvent &event) {
    TaskEventMessage message;
    message.set_task_id(event.task_id.toStdString());
    message.set_event_type(event.event_type.toStdString());
    message.set_sequence_index(event.sequence_index);
    message.set_waypoint_id(event.waypoint_id.toStdString());
    message.set_payload_json(event.payload_json.toStdString());
    return message;
}

TaskSummaryMessage taskSummaryToMessage(const TaskSummary &summary) {
    TaskSummaryMessage message;
    message.set_task_id(summary.task_id.toStdString());
    message.set_task_type(summary.task_type.toStdString());
    message.set_success(summary.success);
    message.set_visited_waypoints(summary.visited_waypoints);
    message.set_payload_json(summary.payload_json.toStdString());
    return message;
}

std::optional<TaskPlan> taskPlanFromMessage(const TaskPlanMessage &message, QString *error_message) {
    TaskPlan plan;
    plan.task_id = QString::fromStdString(message.task_id());
    plan.task_type = QString::fromStdString(message.task_type());
    plan.start_waypoint_id = QString::fromStdString(message.start_waypoint_id());
    plan.terminal_waypoint_id = QString::fromStdString(message.terminal_waypoint_id());
    plan.metadata_json = QString::fromStdString(message.metadata_json());

    if (plan.task_id.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "missing task_id";
        }
        return std::nullopt;
    }
    if (message.waypoints_size() == 0) {
        if (error_message != nullptr) {
            *error_message = "waypoints is empty";
        }
        return std::nullopt;
    }

    for (const TaskWaypointMessage &waypoint_message : message.waypoints()) {
        TaskWaypoint waypoint;
        waypoint.id = QString::fromStdString(waypoint_message.id());
        waypoint.sequence_index = waypoint_message.sequence_index();
        waypoint.x = waypoint_message.x();
        waypoint.y = waypoint_message.y();
        waypoint.z = waypoint_message.z();
        waypoint.action = QString::fromStdString(waypoint_message.action());
        waypoint.payload_json = QString::fromStdString(waypoint_message.payload_json());
        if (waypoint.id.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = "missing waypoint id";
            }
            return std::nullopt;
        }
        plan.waypoints.append(waypoint);
    }

    if (plan.start_waypoint_id.isEmpty()) {
        plan.start_waypoint_id = plan.waypoints.first().id;
    }
    if (plan.terminal_waypoint_id.isEmpty()) {
        plan.terminal_waypoint_id = plan.waypoints.last().id;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

Envelope buildTaskPlanEnvelope(quint64 sequence, const TaskPlan &plan) {
    Envelope envelope = newEnvelope(sequence);
    *envelope.mutable_task_plan() = taskPlanToMessage(plan);
    return envelope;
}

Envelope buildMissionLoadEnvelope(quint64 sequence, const TaskPlan &plan) {
    Envelope envelope = newEnvelope(sequence);
    *envelope.mutable_mission_load() = taskPlanToMessage(plan);
    return envelope;
}

Envelope buildMissionLoadEnvelope(const TaskPlan &plan) {
    return buildMissionLoadEnvelope(0, plan);
}

Envelope buildTaskEventEnvelope(quint64 sequence, const TaskEvent &event) {
    Envelope envelope = newEnvelope(sequence);
    *envelope.mutable_task_event() = taskEventToMessage(event);
    return envelope;
}

Envelope buildTaskSummaryEnvelope(quint64 sequence, const TaskSummary &summary) {
    Envelope envelope = newEnvelope(sequence);
    *envelope.mutable_task_summary() = taskSummaryToMessage(summary);
    return envelope;
}

Envelope buildAckEnvelope(bool success, const QString &message) {
    return buildAckEnvelope(AckResult{success, message});
}

Envelope buildAckEnvelope(const AckResult &result) {
    Envelope envelope = newEnvelope(0);
    auto *payload = envelope.mutable_ack();
    payload->set_success(result.success);
    payload->set_message(result.message.toStdString());
    payload->set_task_id(result.task_id.toStdString());
    payload->set_mission_loaded(result.mission_loaded);
    payload->set_mission_running(result.mission_running);
    payload->set_last_accepted_sequence(result.last_accepted_sequence);
    payload->set_vision_armed(result.vision_armed);
    return envelope;
}

Envelope buildAckEnvelope(const AckResult &result, const CommandState &state) {
    AckResult stateful_result = result;
    const TaskPlan plan = state.activeTaskPlan();
    stateful_result.task_id = plan.task_id;
    stateful_result.mission_loaded = state.isMissionLoaded();
    stateful_result.mission_running = state.isStartRequested() && !state.isStopRequested();
    stateful_result.last_accepted_sequence = state.lastAcceptedSequence();
    stateful_result.vision_armed = state.isVisionTargetingArmed();
    return buildAckEnvelope(stateful_result);
}

QByteArray buildAckBytes(bool success, const QString &message) {
    return buildAckBytes(AckResult{success, message});
}

QByteArray buildAckBytes(const AckResult &result) {
    const Envelope envelope = buildAckEnvelope(result);
    std::string bytes;
    envelope.SerializeToString(&bytes);
    return QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

QByteArray buildAckBytes(const AckResult &result, const CommandState *state) {
    const Envelope envelope = (state == nullptr) ? buildAckEnvelope(result) : buildAckEnvelope(result, *state);
    std::string bytes;
    envelope.SerializeToString(&bytes);
    return QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

} // namespace competition
