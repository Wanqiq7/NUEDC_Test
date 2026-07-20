#include "h_problem_core/tools/planner_cli.h"

#include "competition_core/mission/task_plan_store.h"
#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/planning/mission_geometry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace hcore {

namespace {

PlannerCliResult jsonResult(int exit_code, const QJsonObject &object) {
    return {
        exit_code,
        QJsonDocument(object).toJson(QJsonDocument::Compact),
        {},
    };
}

PlannerCliResult errorResult(int exit_code, const QString &error_code, const QString &message) {
    return jsonResult(exit_code, {
        {"ok", false},
        {"error_code", error_code},
        {"message", message},
    });
}

bool parseStringArray(const QJsonValue &value, QStringList *strings) {
    if (!value.isArray()) {
        return false;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString()) {
            return false;
        }
        strings->append(entry.toString());
    }
    return true;
}

bool isValidNoFlyCell(const QString &cell) {
    const auto decoded = decodeCell(cell);
    return decoded.has_value()
        && decoded->x() < MapWidth
        && decoded->y() < MapHeight;
}

} // namespace

PlannerCliResult runPlannerCliRequest(const QByteArray &request_bytes) {
    QJsonParseError parse_error;
    const QJsonDocument request_document = QJsonDocument::fromJson(request_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !request_document.isObject()) {
        return errorResult(2, "invalid_request", "request must be a JSON object");
    }

    const QJsonObject request = request_document.object();
    if (!request.value("schema").isString()
        || request.value("schema").toString() != "h_planning_request_v1"
        || !request.value("case_path").isString()
        || request.value("case_path").toString().isEmpty()) {
        return errorResult(2, "invalid_request", "invalid schema or case_path");
    }

    QStringList no_fly_cells;
    if (!parseStringArray(request.value("no_fly_cells"), &no_fly_cells)) {
        return errorResult(2, "invalid_request", "no_fly_cells must be an array of strings");
    }
    for (const QString &cell : no_fly_cells) {
        if (!isValidNoFlyCell(cell)) {
            return errorResult(3, "invalid_no_fly_zone", QString("invalid no-fly cell: %1").arg(cell));
        }
    }

    QString error;
    const auto case_config = loadCase(request.value("case_path").toString(), &error);
    if (!case_config.has_value()) {
        return errorResult(3, "case_load_failed", error);
    }

    const auto plan = buildTaskPlan(case_config.value(), no_fly_cells, &error);
    if (!plan.has_value()) {
        return errorResult(3, "planning_failed", error);
    }

    QJsonParseError metadata_error;
    const QJsonDocument metadata_document = QJsonDocument::fromJson(
        plan->metadata_json.toUtf8(), &metadata_error);
    if (metadata_error.error != QJsonParseError::NoError || !metadata_document.isObject()) {
        return errorResult(4, "internal_error", "planner returned invalid metadata");
    }
    const QJsonObject metadata = metadata_document.object();
    QJsonObject success{
        {"ok", true},
        {"plan", competition::taskPlanToJson(plan.value())},
    };
    success["metrics"] = QJsonObject{
        {"estimated_mission_time_s", metadata.value("estimated_mission_time_s")},
        {"planning_optimality", metadata.value("planning_optimality")},
    };
    return jsonResult(0, success);
}

} // namespace hcore
