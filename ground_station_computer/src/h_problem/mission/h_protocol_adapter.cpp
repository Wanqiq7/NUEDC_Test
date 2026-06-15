#include "h_problem/mission/h_protocol_adapter.h"

#include "h_problem_core/protocol/envelope_builder.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <optional>

namespace {

std::optional<QJsonObject> payloadObject(const QString &payload_json, QString *error_message) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload_json.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error_message != nullptr) {
            *error_message = QString("无法解析 H 题 payload_json: %1").arg(parse_error.errorString());
        }
        return std::nullopt;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return document.object();
}

QString firstString(const QJsonObject &object, const QStringList &keys, const QString &fallback = {}) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString()) {
            return value.toString();
        }
    }
    return fallback;
}

int firstInt(const QJsonObject &object, const QStringList &keys, int fallback = 0) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) {
            return value.toInt();
        }
    }
    return fallback;
}

} // namespace

bool HProtocolAdapter::decodeGridConfig(const TaskPlanMessage &message, HGridConfigData *data, QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题任务计划输出参数为空";
        }
        return false;
    }

    const auto plan = hcore::missionPlanFromGridConfig(message, error_message);
    if (!plan.has_value()) {
        return false;
    }

    data->case_id = plan->case_id;
    data->start_cell = plan->start_cell;
    data->no_fly_cells = plan->no_fly_cells;
    data->route = plan->route;
    data->terminal_cell = plan->terminal_cell;
    data->landing_enabled = plan->landing_enabled;
    data->descent_angle_deg = plan->descent_angle_deg.value_or(0.0);
    data->takeoff_anchor_x_cm = plan->takeoff_anchor_x_cm.value_or(0.0);
    data->takeoff_anchor_y_cm = plan->takeoff_anchor_y_cm.value_or(0.0);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HProtocolAdapter::decodeTelemetry(const TaskEventMessage &message, HTelemetryData *data, QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题遥测输出参数为空";
        }
        return false;
    }

    const auto payload = payloadObject(QString::fromStdString(message.payload_json()), error_message);
    if (!payload.has_value()) {
        return false;
    }

    data->current_cell = firstString(payload.value(), {"current_cell", "cell"}, QString::fromStdString(message.waypoint_id()));
    data->step_index = static_cast<int>(message.sequence_index());
    data->visited_cells = firstInt(payload.value(), {"visited_cells", "visited"}, 0);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HProtocolAdapter::decodeDetection(const TaskEventMessage &message, HDetectionData *data, QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题检测输出参数为空";
        }
        return false;
    }

    const auto payload = payloadObject(QString::fromStdString(message.payload_json()), error_message);
    if (!payload.has_value()) {
        return false;
    }

    data->cell_code = firstString(payload.value(), {"cell_code", "cell"}, QString::fromStdString(message.waypoint_id()));
    data->animal_name = firstString(payload.value(), {"animal_name", "animal"});
    data->count = firstInt(payload.value(), {"count"}, 0);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HProtocolAdapter::decodeSummary(const TaskSummaryMessage &message, HSummaryData *data, QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题汇总输出参数为空";
        }
        return false;
    }

    const auto payload = payloadObject(QString::fromStdString(message.payload_json()), error_message);
    if (!payload.has_value()) {
        return false;
    }

    data->totals.clear();
    const QJsonObject totals = payload->value("totals").toObject();
    for (auto iterator = totals.constBegin(); iterator != totals.constEnd(); ++iterator) {
        if (iterator.value().isDouble()) {
            data->totals.insert(iterator.key(), iterator.value().toInt());
        }
    }
    data->visited_cells = static_cast<int>(message.visited_waypoints());
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}
