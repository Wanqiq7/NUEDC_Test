#include "h_problem_core/mission/mission_plan_store.h"

#include "h_problem_core/protocol/envelope_builder.h"
#include "h_problem_core/planning/mission_geometry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace hcore {

namespace {

QJsonArray stringListToJsonArray(const QStringList &strings) {
    QJsonArray array;
    for (const QString &string : strings) {
        array.append(string);
    }
    return array;
}

std::optional<QStringList> requiredStringList(const QJsonObject &object, const char *key, bool allow_empty, QString *error_message) {
    if (!object.contains(key) || !object.value(key).isArray()) {
        if (error_message != nullptr) {
            *error_message = QString("missing or invalid %1").arg(QString::fromUtf8(key));
        }
        return std::nullopt;
    }
    QStringList result;
    for (const QJsonValue entry : object.value(key).toArray()) {
        if (!entry.isString()) {
            if (error_message != nullptr) {
                *error_message = QString("%1 must contain strings").arg(QString::fromUtf8(key));
            }
            return std::nullopt;
        }
        result.append(entry.toString());
    }
    if (!allow_empty && result.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = QString("%1 must contain at least one element").arg(QString::fromUtf8(key));
        }
        return std::nullopt;
    }
    return result;
}

bool requiredString(const QJsonObject &object, const char *key, QString *output, QString *error_message) {
    if (!object.contains(key) || !object.value(key).isString() || object.value(key).toString().isEmpty()) {
        if (error_message != nullptr) {
            *error_message = QString("missing %1").arg(QString::fromUtf8(key));
        }
        return false;
    }
    *output = object.value(key).toString();
    return true;
}

std::optional<double> optionalDouble(const QJsonObject &object, const char *key) {
    if (!object.contains(key) || object.value(key).isNull() || object.value(key).isUndefined()) {
        return std::nullopt;
    }
    if (object.value(key).isDouble()) {
        return object.value(key).toDouble();
    }
    if (object.value(key).isString()) {
        bool ok = false;
        const double converted = object.value(key).toString().toDouble(&ok);
        if (ok) {
            return converted;
        }
    }
    return std::nullopt;
}

std::optional<QJsonObject> readJsonObject(const QString &path, const QString &label, QString *error_message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = file.errorString();
        }
        return std::nullopt;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error_message != nullptr) {
            *error_message = QString("failed to parse %1 JSON (%2)").arg(label, parse_error.errorString());
        }
        return std::nullopt;
    }
    return document.object();
}

bool writeJsonObject(const QJsonObject &object, const QString &output_path, QString *error_message) {
    const QFileInfo output_info(output_path);
    QDir().mkpath(output_info.absolutePath());
    QSaveFile output_file(output_info.filePath());
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (output_file.write(bytes) != bytes.size()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        output_file.cancelWriting();
        return false;
    }
    if (!output_file.commit()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

}

QJsonObject missionPlanToJson(const MissionPlan &plan) {
    QJsonObject object;
    object["message_type"] = "config";
    object["case_id"] = plan.case_id;
    object["start_cell"] = plan.start_cell;
    object["no_fly_cells"] = stringListToJsonArray(plan.no_fly_cells);
    object["route"] = stringListToJsonArray(plan.route);
    object["terminal_cell"] = plan.terminal_cell;
    object["landing_enabled"] = plan.landing_enabled;
    object["descent_angle_deg"] = plan.descent_angle_deg.has_value() ? QJsonValue(plan.descent_angle_deg.value()) : QJsonValue::Null;
    object["takeoff_anchor_x_cm"] = plan.takeoff_anchor_x_cm.has_value() ? QJsonValue(plan.takeoff_anchor_x_cm.value()) : QJsonValue::Null;
    object["takeoff_anchor_y_cm"] = plan.takeoff_anchor_y_cm.has_value() ? QJsonValue(plan.takeoff_anchor_y_cm.value()) : QJsonValue::Null;
    return object;
}

std::optional<MissionPlan> missionPlanFromJsonObject(const QJsonObject &object, QString *error_message) {
    MissionPlan plan;
    if (!requiredString(object, "case_id", &plan.case_id, error_message)
        || !requiredString(object, "start_cell", &plan.start_cell, error_message)
        || !requiredString(object, "terminal_cell", &plan.terminal_cell, error_message)) {
        return std::nullopt;
    }

    const auto no_fly_cells = requiredStringList(object, "no_fly_cells", true, error_message);
    if (!no_fly_cells.has_value()) {
        return std::nullopt;
    }
    const auto route = requiredStringList(object, "route", false, error_message);
    if (!route.has_value()) {
        return std::nullopt;
    }
    plan.no_fly_cells = no_fly_cells.value();
    plan.route = route.value();

    if (!object.contains("landing_enabled") || !object.value("landing_enabled").isBool()) {
        if (error_message != nullptr) {
            *error_message = "missing or invalid landing_enabled";
        }
        return std::nullopt;
    }
    plan.landing_enabled = object.value("landing_enabled").toBool();
    plan.descent_angle_deg = optionalDouble(object, "descent_angle_deg");
    plan.takeoff_anchor_x_cm = optionalDouble(object, "takeoff_anchor_x_cm");
    plan.takeoff_anchor_y_cm = optionalDouble(object, "takeoff_anchor_y_cm");

    if (error_message != nullptr) {
        error_message->clear();
    }
    return plan;
}

bool validateMissionPlan(const MissionPlan &plan, QString *error_message) {
    if (plan.case_id.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "missing case_id";
        }
        return false;
    }
    if (plan.start_cell.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "missing start_cell";
        }
        return false;
    }
    if (plan.terminal_cell.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "missing terminal_cell";
        }
        return false;
    }
    if (plan.route.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "route is empty";
        }
        return false;
    }

    QString decode_error;
    if (!decodeCell(plan.start_cell, &decode_error).has_value()
        || !decodeCell(plan.terminal_cell, &decode_error).has_value()) {
        if (error_message != nullptr) {
            *error_message = "invalid cell code";
        }
        return false;
    }
    for (const QString &cell : plan.no_fly_cells) {
        if (!decodeCell(cell, &decode_error).has_value()) {
            if (error_message != nullptr) {
                *error_message = "invalid cell code";
            }
            return false;
        }
    }
    for (const QString &cell : plan.route) {
        if (!decodeCell(cell, &decode_error).has_value()) {
            if (error_message != nullptr) {
                *error_message = "invalid cell code";
            }
            return false;
        }
    }

    const QSet<QString> route_cells(plan.route.begin(), plan.route.end());
    const QSet<QString> no_fly_cells(plan.no_fly_cells.begin(), plan.no_fly_cells.end());
    if (!(route_cells & no_fly_cells).isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "route intersects no_fly_cells";
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool storeMissionPlan(const MissionPlan &plan, const QString &output_path, QString *error_message) {
    QString validation_error;
    if (!validateMissionPlan(plan, &validation_error)) {
        if (error_message != nullptr) {
            *error_message = validation_error;
        }
        return false;
    }

    return competition::storeTaskPlan(taskPlanFromMissionPlan(plan), output_path, error_message);
}

std::optional<MissionPlan> loadMissionPlan(const QString &path, QString *error_message) {
    QString task_plan_error;
    const auto task_plan = competition::loadTaskPlan(path, &task_plan_error);
    if (task_plan.has_value()) {
        return missionPlanFromTaskPlan(task_plan.value(), error_message);
    }

    const auto object = readJsonObject(path, "mission plan", error_message);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return missionPlanFromJsonObject(object.value(), error_message);
}

} // namespace hcore
