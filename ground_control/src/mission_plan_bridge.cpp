#include "mission_plan_bridge.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>

namespace {

QString findRepositoryRootFromCasePath(const QString &case_path) {
    QFileInfo file_info(case_path);
    QDir dir = file_info.isAbsolute() ? file_info.absoluteDir() : QDir::current();
    if (!file_info.isAbsolute()) {
        dir = QFileInfo(QDir::current().filePath(case_path)).absoluteDir();
    }

    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("airborne")) && dir.exists(QStringLiteral("shared"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QFileInfo(file_info.isAbsolute() ? file_info.filePath() : QDir::current().filePath(case_path)).absolutePath();
}

QStringList requiredStringList(const QJsonObject &object, const char *key, QString *error, bool allow_empty = false) {
    const QString readable_key = QString::fromUtf8(key);

    if (!object.contains(key)) {
        *error = QString("missing %1").arg(readable_key);
        return {};
    }

    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        *error = QString("invalid %1").arg(readable_key);
        return {};
    }

    const QJsonArray array = value.toArray();
    QStringList result;
    result.reserve(array.size());

    for (const QJsonValue entry : array) {
        if (!entry.isString()) {
            *error = QString("%1 must contain strings").arg(readable_key);
            return {};
        }
        result.append(entry.toString());
    }

    if (!allow_empty && result.isEmpty()) {
        *error = QString("%1 must contain at least one element").arg(readable_key);
        return {};
    }

    return result;
}

bool hasStringField(const QJsonObject &object, const char *key, QString *output, QString *error) {
    if (!object.contains(key) || !object.value(key).isString()) {
        *error = QString("missing or invalid %1").arg(QString::fromUtf8(key));
        return false;
    }
    *output = object.value(key).toString();
    return true;
}

std::optional<double> optionalDouble(const QJsonObject &object, const char *key) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double converted = value.toString().toDouble(&ok);
        if (ok) {
            return converted;
        }
    }
    return std::nullopt;
}

} // namespace

MissionPlanResult MissionPlanBridge::parsePlannerOutput(const QByteArray &stdout_bytes) {
    MissionPlanResult result;
    if (stdout_bytes.isEmpty()) {
        result.error_message = "planner stdout is empty";
        return result;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(stdout_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error_message = QString("failed to parse planner output (%1)").arg(parse_error.errorString());
        return result;
    }

    const QJsonObject object = document.object();
    QString error_holder;
    if (!hasStringField(object, "case_id", &result.plan.case_id, &error_holder)) {
        result.error_message = error_holder;
        return result;
    }

    if (!hasStringField(object, "start_cell", &result.plan.start_cell, &error_holder)) {
        result.error_message = error_holder;
        return result;
    }

    if (!hasStringField(object, "terminal_cell", &result.plan.terminal_cell, &error_holder)) {
        result.error_message = error_holder;
        return result;
    }

    const QStringList no_fly_cells = requiredStringList(object, "no_fly_cells", &error_holder, true);
    if (error_holder.isEmpty()) {
        result.plan.no_fly_cells = no_fly_cells;
    } else {
        result.error_message = error_holder;
        return result;
    }

    const QStringList route = requiredStringList(object, "route", &error_holder);
    if (!route.isEmpty()) {
        result.plan.route = route;
    } else {
        if (error_holder.isEmpty()) {
            result.error_message = "route must contain at least one segment";
        } else {
            result.error_message = error_holder;
        }
        return result;
    }

    if (!object.contains("landing_enabled") || !object.value("landing_enabled").isBool()) {
        result.error_message = "missing or invalid landing_enabled";
        return result;
    }
    result.plan.landing_enabled = object.value("landing_enabled").toBool();

    result.plan.descent_angle_deg = optionalDouble(object, "descent_angle_deg");
    result.plan.takeoff_anchor_x_cm = optionalDouble(object, "takeoff_anchor_x_cm");
    result.plan.takeoff_anchor_y_cm = optionalDouble(object, "takeoff_anchor_y_cm");

    result.ok = true;
    return result;
}

MissionPlanResult MissionPlanBridge::generatePlan(const QString &case_path, const QStringList &no_fly_cells) const {
    MissionPlanResult result;
    const QFileInfo case_file_info(case_path);
    const QString absolute_case_path = case_file_info.isAbsolute()
        ? case_file_info.filePath()
        : QDir::current().filePath(case_path);
    const QDir project_root_dir(findRepositoryRootFromCasePath(absolute_case_path));

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONPATH", project_root_dir.filePath("airborne"));
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(project_root_dir.absolutePath());

    QStringList arguments;
    arguments << "-m"
              << "uav_testbed.export_mission_plan"
              << "--case"
              << absolute_case_path;
    if (!no_fly_cells.isEmpty()) {
        arguments << "--no-fly-cells";
        arguments << no_fly_cells;
    }

    process.start("python3", arguments);
    if (!process.waitForStarted()) {
        result.error_message = QString("failed to start planner (%1)").arg(process.errorString());
        return result;
    }

    if (!process.waitForFinished()) {
        result.error_message = QString("planner timeout or crash (%1)").arg(process.errorString());
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderr_content = QString::fromUtf8(process.readAllStandardError()).trimmed();
        result.error_message = QString("planner exited abnormally (%1)").arg(stderr_content.isEmpty() ? process.errorString() : stderr_content);
        return result;
    }

    const QByteArray stdout_bytes = process.readAllStandardOutput();
    result = parsePlannerOutput(stdout_bytes);
    if (!result.ok && result.error_message.isEmpty()) {
        result.error_message = "planner output missing required fields";
    }
    return result;
}
