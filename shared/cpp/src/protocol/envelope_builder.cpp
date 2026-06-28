#include "h_problem_core/protocol/envelope_builder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hcore {

namespace {

QJsonArray stringListToJsonArray(const QStringList &strings) {
    QJsonArray array;
    for (const QString &string : strings) {
        array.append(string);
    }
    return array;
}

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<QJsonObject> jsonObjectFromString(const QString &json, const QString &field_name, QString *error_message) {
    if (json.isEmpty()) {
        return QJsonObject();
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error_message != nullptr) {
            *error_message = QString("invalid %1 JSON").arg(field_name);
        }
        return std::nullopt;
    }
    return document.object();
}

QStringList stringListFromJsonArray(const QJsonValue &value) {
    QStringList strings;
    if (!value.isArray()) {
        return strings;
    }
    for (const QJsonValue entry : value.toArray()) {
        if (entry.isString()) {
            strings.append(entry.toString());
        }
    }
    return strings;
}

QMap<QString, quint32> totalsFromJson(const QString &payload_json) {
    QMap<QString, quint32> totals;
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload_json.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        return totals;
    }
    const QJsonObject object = document.object();
    const QJsonObject totals_object = object.value("totals").toObject();
    for (auto iterator = totals_object.constBegin(); iterator != totals_object.constEnd(); ++iterator) {
        totals.insert(iterator.key(), static_cast<quint32>(iterator.value().toInt()));
    }
    return totals;
}

}

competition::TaskPlan taskPlanFromMissionPlan(const MissionPlan &plan) {
    competition::TaskPlan task_plan;
    task_plan.task_id = plan.case_id;
    task_plan.task_type = "h_problem";
    task_plan.start_waypoint_id = plan.start_cell;
    task_plan.terminal_waypoint_id = plan.terminal_cell;

    QJsonObject metadata;
    metadata["case_id"] = plan.case_id;
    metadata["start_cell"] = plan.start_cell;
    metadata["no_fly_cells"] = stringListToJsonArray(plan.no_fly_cells);
    metadata["terminal_cell"] = plan.terminal_cell;
    metadata["landing_enabled"] = plan.landing_enabled;
    metadata["descent_angle_deg"] = plan.descent_angle_deg.has_value() ? QJsonValue(plan.descent_angle_deg.value()) : QJsonValue::Null;
    metadata["takeoff_anchor_x_cm"] = plan.takeoff_anchor_x_cm.has_value() ? QJsonValue(plan.takeoff_anchor_x_cm.value()) : QJsonValue::Null;
    metadata["takeoff_anchor_y_cm"] = plan.takeoff_anchor_y_cm.has_value() ? QJsonValue(plan.takeoff_anchor_y_cm.value()) : QJsonValue::Null;
    task_plan.metadata_json = compactJson(metadata);

    for (int index = 0; index < plan.route.size(); ++index) {
        competition::TaskWaypoint waypoint;
        waypoint.id = plan.route.at(index);
        waypoint.sequence_index = static_cast<quint32>(index);
        waypoint.action = "navigate";
        QJsonObject payload;
        payload["cell"] = waypoint.id;
        waypoint.payload_json = compactJson(payload);
        task_plan.waypoints.append(waypoint);
    }

    return task_plan;
}

std::optional<MissionPlan> missionPlanFromTaskPlan(const competition::TaskPlan &task_plan, QString *error_message) {
    MissionPlan plan;
    plan.case_id = task_plan.task_id;
    plan.start_cell = task_plan.start_waypoint_id;
    plan.terminal_cell = task_plan.terminal_waypoint_id;

    const auto metadata = jsonObjectFromString(task_plan.metadata_json, "metadata_json", error_message);
    if (!metadata.has_value()) {
        return std::nullopt;
    }
    if (metadata->contains("case_id") && metadata->value("case_id").isString()) {
        plan.case_id = metadata->value("case_id").toString();
    }
    if (metadata->contains("start_cell") && metadata->value("start_cell").isString()) {
        plan.start_cell = metadata->value("start_cell").toString();
    }
    if (metadata->contains("terminal_cell") && metadata->value("terminal_cell").isString()) {
        plan.terminal_cell = metadata->value("terminal_cell").toString();
    }
    plan.no_fly_cells = stringListFromJsonArray(metadata->value("no_fly_cells"));
    plan.landing_enabled = metadata->value("landing_enabled").toBool(false);
    if (metadata->contains("descent_angle_deg") && metadata->value("descent_angle_deg").isDouble()) {
        plan.descent_angle_deg = metadata->value("descent_angle_deg").toDouble();
    }
    if (metadata->contains("takeoff_anchor_x_cm") && metadata->value("takeoff_anchor_x_cm").isDouble()) {
        plan.takeoff_anchor_x_cm = metadata->value("takeoff_anchor_x_cm").toDouble();
    }
    if (metadata->contains("takeoff_anchor_y_cm") && metadata->value("takeoff_anchor_y_cm").isDouble()) {
        plan.takeoff_anchor_y_cm = metadata->value("takeoff_anchor_y_cm").toDouble();
    }

    for (const competition::TaskWaypoint &waypoint : task_plan.waypoints) {
        plan.route.append(waypoint.id);
    }
    if (plan.start_cell.isEmpty() && !plan.route.isEmpty()) {
        plan.start_cell = plan.route.first();
    }
    if (plan.terminal_cell.isEmpty() && !plan.route.isEmpty()) {
        plan.terminal_cell = plan.route.last();
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

Envelope buildGridConfigEnvelope(quint64 sequence, const MissionPlan &plan) {
    return buildTaskPlanEnvelope(sequence, taskPlanFromMissionPlan(plan));
}

Envelope buildMissionLoadEnvelope(const MissionPlan &plan) {
    return buildMissionLoadEnvelope(taskPlanFromMissionPlan(plan));
}

Envelope buildTelemetryEnvelope(quint64 sequence, const QString &cell, quint32 step_index, quint32 visited_cells) {
    QJsonObject payload;
    payload["current_cell"] = cell;
    payload["visited_cells"] = static_cast<int>(visited_cells);

    competition::TaskEvent event;
    event.event_type = "telemetry";
    event.sequence_index = step_index;
    event.waypoint_id = cell;
    event.payload_json = compactJson(payload);
    return buildTaskEventEnvelope(sequence, event);
}

Envelope buildDetectionEnvelope(quint64 sequence, const QString &cell, const QString &animal_name, quint32 count) {
    QJsonObject payload;
    payload["cell_code"] = cell;
    payload["animal_name"] = animal_name;
    payload["count"] = static_cast<int>(count);

    competition::TaskEvent event;
    event.event_type = "detection";
    event.waypoint_id = cell;
    event.payload_json = compactJson(payload);
    return buildTaskEventEnvelope(sequence, event);
}

Envelope buildSummaryEnvelope(quint64 sequence, const QMap<QString, quint32> &totals, quint32 visited_cells) {
    QJsonObject totals_object;
    for (auto iterator = totals.cbegin(); iterator != totals.cend(); ++iterator) {
        totals_object[iterator.key()] = static_cast<int>(iterator.value());
    }
    QJsonObject payload;
    payload["totals"] = totals_object;

    competition::TaskSummary summary;
    summary.task_type = "h_problem";
    summary.success = true;
    summary.visited_waypoints = visited_cells;
    summary.payload_json = compactJson(payload);
    return buildTaskSummaryEnvelope(sequence, summary);
}

std::optional<MissionPlan> missionPlanFromGridConfig(const TaskPlanMessage &config, QString *error_message) {
    const auto task_plan = taskPlanFromMessage(config, error_message);
    if (!task_plan.has_value()) {
        return std::nullopt;
    }
    return missionPlanFromTaskPlan(task_plan.value(), error_message);
}

} // namespace hcore
