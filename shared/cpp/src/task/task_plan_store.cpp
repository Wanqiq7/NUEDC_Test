#include "competition_core/mission/task_plan_store.h"

#include "competition_core/storage/json_codec.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace competition {

namespace {

QJsonArray waypointsToJsonArray(const QVector<TaskWaypoint> &waypoints) {
    QJsonArray array;
    for (const TaskWaypoint &waypoint : waypoints) {
        QJsonObject object;
        object["id"] = waypoint.id;
        object["sequence_index"] = static_cast<int>(waypoint.sequence_index);
        object["x"] = waypoint.x;
        object["y"] = waypoint.y;
        object["z"] = waypoint.z;
        object["action"] = waypoint.action;
        object["payload_json"] = waypoint.payload_json;
        array.append(object);
    }
    return array;
}

} // namespace

QJsonObject taskPlanToJson(const TaskPlan &plan) {
    QJsonObject object;
    object["message_type"] = "task_plan";
    object["task_id"] = plan.task_id;
    object["task_type"] = plan.task_type;
    object["start_waypoint_id"] = plan.start_waypoint_id;
    object["terminal_waypoint_id"] = plan.terminal_waypoint_id;
    object["waypoints"] = waypointsToJsonArray(plan.waypoints);
    object["metadata_json"] = plan.metadata_json;
    return object;
}

std::optional<TaskPlan> taskPlanFromJsonObject(const QJsonObject &object, QString *error_message) {
    TaskPlan plan;
    if (!requiredString(object, "task_id", &plan.task_id, error_message)) {
        return std::nullopt;
    }
    if (object.contains("task_type") && object.value("task_type").isString()) {
        plan.task_type = object.value("task_type").toString();
    }
    if (object.contains("start_waypoint_id") && object.value("start_waypoint_id").isString()) {
        plan.start_waypoint_id = object.value("start_waypoint_id").toString();
    }
    if (object.contains("terminal_waypoint_id") && object.value("terminal_waypoint_id").isString()) {
        plan.terminal_waypoint_id = object.value("terminal_waypoint_id").toString();
    }
    if (object.contains("metadata_json") && object.value("metadata_json").isString()) {
        plan.metadata_json = object.value("metadata_json").toString();
    }

    if (!object.contains("waypoints") || !object.value("waypoints").isArray()) {
        if (error_message != nullptr) {
            *error_message = "missing or invalid waypoints";
        }
        return std::nullopt;
    }
    for (const QJsonValue entry : object.value("waypoints").toArray()) {
        if (!entry.isObject()) {
            if (error_message != nullptr) {
                *error_message = "waypoints must contain objects";
            }
            return std::nullopt;
        }
        const QJsonObject waypoint_object = entry.toObject();
        TaskWaypoint waypoint;
        if (!requiredString(waypoint_object, "id", &waypoint.id, error_message)) {
            return std::nullopt;
        }
        waypoint.sequence_index = static_cast<quint32>(waypoint_object.value("sequence_index").toInt());
        waypoint.x = waypoint_object.value("x").toDouble();
        waypoint.y = waypoint_object.value("y").toDouble();
        waypoint.z = waypoint_object.value("z").toDouble();
        waypoint.action = waypoint_object.value("action").toString();
        waypoint.payload_json = waypoint_object.value("payload_json").toString();
        plan.waypoints.append(waypoint);
    }

    if (plan.start_waypoint_id.isEmpty() && !plan.waypoints.isEmpty()) {
        plan.start_waypoint_id = plan.waypoints.first().id;
    }
    if (plan.terminal_waypoint_id.isEmpty() && !plan.waypoints.isEmpty()) {
        plan.terminal_waypoint_id = plan.waypoints.last().id;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

bool validateTaskPlan(const TaskPlan &plan, QString *error_message) {
    if (plan.task_id.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "missing task_id";
        }
        return false;
    }
    if (plan.waypoints.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "waypoints is empty";
        }
        return false;
    }
    for (const TaskWaypoint &waypoint : plan.waypoints) {
        if (waypoint.id.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = "missing waypoint id";
            }
            return false;
        }
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool storeTaskPlan(const TaskPlan &plan, const QString &output_path, QString *error_message) {
    QString validation_error;
    if (!validateTaskPlan(plan, &validation_error)) {
        if (error_message != nullptr) {
            *error_message = validation_error;
        }
        return false;
    }
    return writeJsonObject(taskPlanToJson(plan), output_path, error_message);
}

std::optional<TaskPlan> loadTaskPlan(const QString &path, QString *error_message) {
    const auto object = readJsonObject(path, "task plan", error_message);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return taskPlanFromJsonObject(object.value(), error_message);
}

} // namespace competition
