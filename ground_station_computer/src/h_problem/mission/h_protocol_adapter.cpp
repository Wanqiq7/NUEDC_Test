#include "h_problem/mission/h_protocol_adapter.h"

#include "competition_core/mission/task_plan_store.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/planning/mission_geometry.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
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

double firstDouble(const QJsonObject &object, const QStringList &keys, double fallback = 0.0) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) {
            return value.toDouble();
        }
    }
    return fallback;
}

constexpr double ValidationEpsilon = 1e-4;

bool reject(QString *error_message, const QString &message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
    return false;
}

bool nearlyEqual(double left, double right, double tolerance = ValidationEpsilon) {
    return std::abs(left - right) <= tolerance;
}

double headingDifferenceDeg(double left, double right) {
    const double normalized = std::fmod(left - right + 540.0, 360.0) - 180.0;
    return std::abs(normalized);
}

bool isGridCell(const QString &cell) {
    const auto decoded = hcore::decodeCell(cell);
    return decoded.has_value()
        && decoded->x() >= 0
        && decoded->x() < hcore::MapWidth
        && decoded->y() >= 0
        && decoded->y() < hcore::MapHeight;
}

bool requiredString(const QJsonObject &metadata, const char *key, QString *value, QString *error_message) {
    const QString field_name = QString::fromUtf8(key);
    const QJsonValue field = metadata.value(field_name);
    if (!field.isString() || field.toString().isEmpty()) {
        return reject(error_message, QString("H 题任务计划缺少或错误的 metadata 字段: %1").arg(field_name));
    }
    *value = field.toString();
    return true;
}

bool requiredFiniteNumber(const QJsonObject &metadata, const char *key, double *value, QString *error_message) {
    const QString field_name = QString::fromUtf8(key);
    const QJsonValue field = metadata.value(field_name);
    if (!field.isDouble() || !std::isfinite(field.toDouble())) {
        return reject(error_message, QString("H 题任务计划缺少或错误的 metadata 数值字段: %1").arg(field_name));
    }
    *value = field.toDouble();
    return true;
}

bool routeCoversEveryLegalCell(const QStringList &route, const QSet<QString> &no_fly_cells) {
    QSet<QString> legal_cells;
    for (int y_index = 0; y_index < hcore::MapHeight; ++y_index) {
        for (int x_index = 0; x_index < hcore::MapWidth; ++x_index) {
            const QString cell = hcore::encodeCell(x_index, y_index);
            if (!no_fly_cells.contains(cell)) {
                legal_cells.insert(cell);
            }
        }
    }
    return QSet<QString>(route.cbegin(), route.cend()) == legal_cells;
}

bool validateLandingMetadata(
    const QString &start_cell,
    const QString &terminal_cell,
    const QSet<QString> &no_fly_cells,
    double descent_angle_deg,
    double takeoff_anchor_x_cm,
    double takeoff_anchor_y_cm,
    double touchdown_x_cm,
    double touchdown_y_cm,
    double descent_run_cm,
    double descent_heading_deg,
    double cruise_height_cm,
    double estimated_mission_time_s,
    QString *error_message) {
    if (descent_angle_deg <= 0.0
        || descent_angle_deg >= 90.0
        || descent_run_cm <= 0.0
        || estimated_mission_time_s < 0.0) {
        return reject(error_message, "H 题降落 metadata 数值范围非法");
    }

    const hcore::PointCm takeoff_anchor{takeoff_anchor_x_cm, takeoff_anchor_y_cm};
    const hcore::PointCm touchdown{touchdown_x_cm, touchdown_y_cm};
    const auto start_center = hcore::cellCodeCenterCm(start_cell, hcore::MapHeight);
    const auto terminal_center = hcore::cellCodeCenterCm(terminal_cell, hcore::MapHeight);
    if (!start_center.has_value() || !terminal_center.has_value()) {
        return reject(error_message, "H 题起飞格或终点格不在固定网格内");
    }
    if (!hcore::descentCorridorIsClear(
            hcore::MapWidth, hcore::MapHeight, takeoff_anchor, start_center.value(), no_fly_cells)) {
        return reject(error_message, "起飞锚点到起飞格的通道穿过禁飞区");
    }

    const double actual_run_cm = hcore::euclideanDistanceCm(terminal_center.value(), touchdown);
    const double actual_heading_deg = hcore::headingDegrees(terminal_center.value(), touchdown);
    if (!nearlyEqual(actual_run_cm, descent_run_cm)
        || headingDifferenceDeg(actual_heading_deg, descent_heading_deg) > ValidationEpsilon) {
        return reject(error_message, "降落 metadata 与终点下降向量不一致");
    }
    if (!hcore::descentCorridorIsClear(
            hcore::MapWidth, hcore::MapHeight, terminal_center.value(), touchdown, no_fly_cells)) {
        return reject(error_message, "终点下降通道穿过禁飞区");
    }

    hcore::LandingProfile landing;
    landing.takeoff_anchor_cm = takeoff_anchor;
    landing.cruise_height_cm = cruise_height_cm;
    landing.descent_angle_deg = descent_angle_deg;
    landing.descent_angle_tolerance_deg = 5.0;
    landing.touchdown_radius_cm = hcore::euclideanDistanceCm(takeoff_anchor, touchdown);
    landing.preferred_heading_deg = descent_heading_deg;
    landing.heading_tolerance_deg = 0.0;
    const auto approach = hcore::landingApproachForTerminal(
        hcore::MapWidth, hcore::MapHeight, terminal_cell, no_fly_cells, landing);
    if (!approach.has_value()
        || !nearlyEqual(approach->horizontal_run_cm, descent_run_cm)
        || !nearlyEqual(approach->touchdown_point_cm.x_cm, touchdown.x_cm)
        || !nearlyEqual(approach->touchdown_point_cm.y_cm, touchdown.y_cm)) {
        return reject(error_message, "降落 metadata 未定义有效的下降进场");
    }
    return true;
}

} // namespace

bool HProtocolAdapter::validateTaskPlan(const competition::TaskPlan &plan, QString *error_message) {
    if (!competition::validateTaskPlan(plan, error_message)) {
        return false;
    }
    if (plan.task_type != "h_problem") {
        return reject(error_message, "任务计划类型不是 H 题");
    }

    const auto metadata = payloadObject(plan.metadata_json, error_message);
    if (!metadata.has_value()) {
        return false;
    }

    QString case_id;
    QString start_cell;
    QString terminal_cell;
    if (!requiredString(metadata.value(), "case_id", &case_id, error_message)
        || !requiredString(metadata.value(), "start_cell", &start_cell, error_message)
        || !requiredString(metadata.value(), "terminal_cell", &terminal_cell, error_message)) {
        return false;
    }
    const QJsonValue execution_contract = metadata->value("execution_contract");
    if (!execution_contract.isString()) {
        return reject(error_message, "H 题任务计划 execution_contract 必须为字符串");
    }
    if (!metadata->value("landing_enabled").isBool() || !metadata->value("landing_enabled").toBool()) {
        return reject(error_message, "H 题任务计划 landing_enabled 必须为 true");
    }

    const QJsonValue no_fly_value = metadata->value("no_fly_cells");
    if (!no_fly_value.isArray()) {
        return reject(error_message, "H 题任务计划 no_fly_cells 必须为数组");
    }
    QSet<QString> no_fly_cells;
    for (const QJsonValue &entry : no_fly_value.toArray()) {
        if (!entry.isString() || !isGridCell(entry.toString()) || no_fly_cells.contains(entry.toString())) {
            return reject(error_message, "H 题任务计划 no_fly_cells 必须是唯一的合法网格");
        }
        no_fly_cells.insert(entry.toString());
    }

    double descent_angle_deg = 0.0;
    double takeoff_anchor_x_cm = 0.0;
    double takeoff_anchor_y_cm = 0.0;
    double touchdown_x_cm = 0.0;
    double touchdown_y_cm = 0.0;
    double descent_run_cm = 0.0;
    double descent_heading_deg = 0.0;
    double cruise_height_cm = 0.0;
    double estimated_mission_time_s = 0.0;
    if (!requiredFiniteNumber(metadata.value(), "descent_angle_deg", &descent_angle_deg, error_message)
        || !requiredFiniteNumber(metadata.value(), "takeoff_anchor_x_cm", &takeoff_anchor_x_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "takeoff_anchor_y_cm", &takeoff_anchor_y_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "touchdown_x_cm", &touchdown_x_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "touchdown_y_cm", &touchdown_y_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "descent_run_cm", &descent_run_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "descent_heading_deg", &descent_heading_deg, error_message)
        || !requiredFiniteNumber(metadata.value(), "cruise_height_cm", &cruise_height_cm, error_message)
        || !requiredFiniteNumber(metadata.value(), "estimated_mission_time_s", &estimated_mission_time_s, error_message)) {
        return false;
    }
    if (cruise_height_cm <= 0.0) {
        return reject(error_message, "H 题任务计划 cruise_height_cm 必须为正数");
    }

    const QJsonValue optimality = metadata->value("planning_optimality");
    if (!optimality.isString()
        || !QSet<QString>{"proven_optimal", "best_effort", "search_limit_reached"}.contains(optimality.toString())) {
        return reject(error_message, "H 题任务计划 planning_optimality 非法");
    }
    const QJsonValue warnings = metadata->value("planning_warnings");
    if (!warnings.isArray()) {
        return reject(error_message, "H 题任务计划 planning_warnings 必须为数组");
    }
    for (const QJsonValue &warning : warnings.toArray()) {
        if (!warning.isString()) {
            return reject(error_message, "H 题任务计划 planning_warnings 只能包含字符串");
        }
    }

    if (case_id != plan.task_id) {
        return reject(error_message, "task_id 与 metadata case_id 不一致");
    }
    if (!isGridCell(start_cell) || !isGridCell(terminal_cell)) {
        return reject(error_message, "H 题起飞格或终点格不在固定网格内");
    }
    if (no_fly_cells.contains(start_cell) || no_fly_cells.contains(terminal_cell)) {
        return reject(error_message, "H 题起飞格或终点格位于禁飞区");
    }

    QStringList route;
    route.reserve(plan.waypoints.size());
    const competition::TaskWaypoint *land = nullptr;
    for (int index = 0; index < plan.waypoints.size(); ++index) {
        const competition::TaskWaypoint &waypoint = plan.waypoints.at(index);
        if (waypoint.sequence_index != static_cast<quint32>(index)
            || !std::isfinite(waypoint.x)
            || !std::isfinite(waypoint.y)
            || !std::isfinite(waypoint.z)) {
            return reject(error_message, "H 题航点序号和坐标必须有效");
        }
        if (waypoint.action == "takeoff" || waypoint.action == "navigate") {
            if (!isGridCell(waypoint.id)) {
                return reject(error_message, "H 题巡查航点必须是合法网格");
            }
            route.append(waypoint.id);
        } else if (waypoint.action == "land") {
            if (land != nullptr) {
                return reject(error_message, "H 题只能包含一个 land 航点");
            }
            land = &waypoint;
        } else {
            return reject(error_message, "H 题航点 action 非法");
        }
    }

    if (execution_contract.toString() != hcore::HExecutionContract
        || route.isEmpty()
        || plan.waypoints.first().action != "takeoff"
        || land == nullptr
        || land != &plan.waypoints.last()
        || land->id != hcore::HTouchdownWaypointId
        || plan.terminal_waypoint_id != hcore::HTouchdownWaypointId) {
        return reject(error_message, "H 题执行契约不匹配");
    }
    for (int index = 1; index < route.size(); ++index) {
        if (plan.waypoints.at(index).action != "navigate") {
            return reject(error_message, "H 题只能在首个航点执行 takeoff");
        }
    }
    if (route.first() != "A9B1"
        || plan.start_waypoint_id != route.first()
        || start_cell != route.first()
        || terminal_cell != route.last()) {
        return reject(error_message, "H 题任务计划端点与执行契约不一致");
    }

    const double cruise_height_m = cruise_height_cm / 100.0;
    for (int index = 0; index < route.size(); ++index) {
        const competition::TaskWaypoint &waypoint = plan.waypoints.at(index);
        const auto center_cm = hcore::cellCodeCenterCm(waypoint.id, hcore::MapHeight);
        if (!center_cm.has_value()) {
            return reject(error_message, "H 题巡查航点必须是合法网格");
        }
        const hcore::MissionPointM center_m = hcore::fieldPointToMissionMeters(center_cm.value());
        if (!nearlyEqual(waypoint.x, center_m.x_m)
            || !nearlyEqual(waypoint.y, center_m.y_m)
            || !nearlyEqual(waypoint.z, cruise_height_m)) {
            return reject(error_message, "H 题巡查航点坐标与网格或巡航高度不一致");
        }
        if (no_fly_cells.contains(waypoint.id)) {
            return reject(error_message, "H 题航线穿过禁飞区");
        }
        if (index > 0) {
            const QPoint previous = hcore::decodeCell(route.at(index - 1)).value();
            const QPoint current = hcore::decodeCell(waypoint.id).value();
            if (std::abs(current.x() - previous.x()) + std::abs(current.y() - previous.y()) != 1) {
                return reject(error_message, "H 题航线必须按正交相邻方格移动");
            }
        }
    }
    if (!routeCoversEveryLegalCell(route, no_fly_cells)) {
        return reject(error_message, "H 题航线未覆盖全部合法方格");
    }

    const hcore::MissionPointM touchdown_m =
        hcore::fieldPointToMissionMeters({touchdown_x_cm, touchdown_y_cm});
    if (!nearlyEqual(land->x, touchdown_m.x_m)
        || !nearlyEqual(land->y, touchdown_m.y_m)
        || !nearlyEqual(land->z, 0.0)) {
        return reject(error_message, "H 题 land 航点与降落 metadata 不一致");
    }
    if (!validateLandingMetadata(
            start_cell,
            terminal_cell,
            no_fly_cells,
            descent_angle_deg,
            takeoff_anchor_x_cm,
            takeoff_anchor_y_cm,
            touchdown_x_cm,
            touchdown_y_cm,
            descent_run_cm,
            descent_heading_deg,
            cruise_height_cm,
            estimated_mission_time_s,
            error_message)) {
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HProtocolAdapter::decodeTaskPlan(
    const competition::TaskPlan &plan,
    HGridConfigData *data,
    QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题任务计划输出参数为空";
        }
        return false;
    }

    if (!validateTaskPlan(plan, error_message)) {
        return false;
    }
    const auto metadata = payloadObject(plan.metadata_json, error_message);
    if (!metadata.has_value()) {
        return false;
    }

    data->case_id = metadata->value("case_id").toString();
    data->start_cell = metadata->value("start_cell").toString();
    data->no_fly_cells.clear();
    const QJsonArray no_fly_cells = metadata->value("no_fly_cells").toArray();
    for (const QJsonValue &cell : no_fly_cells) {
        if (cell.isString()) {
            data->no_fly_cells.append(cell.toString());
        }
    }
    data->route.clear();
    for (const competition::TaskWaypoint &waypoint : plan.waypoints) {
        if (waypoint.action == "takeoff" || waypoint.action == "navigate") {
            data->route.append(waypoint.id);
        }
    }
    data->terminal_cell = metadata->value("terminal_cell").toString();
    data->landing_enabled = metadata->value("landing_enabled").toBool();
    data->descent_angle_deg = metadata->value("descent_angle_deg").toDouble();
    data->takeoff_anchor_x_cm = metadata->value("takeoff_anchor_x_cm").toDouble();
    data->takeoff_anchor_y_cm = metadata->value("takeoff_anchor_y_cm").toDouble();
    data->touchdown_x_cm = metadata->value("touchdown_x_cm").toDouble();
    data->touchdown_y_cm = metadata->value("touchdown_y_cm").toDouble();
    data->descent_run_cm = metadata->value("descent_run_cm").toDouble();
    data->descent_heading_deg = metadata->value("descent_heading_deg").toDouble();
    data->estimated_mission_time_s = metadata->value("estimated_mission_time_s").toDouble();
    data->planning_optimality = metadata->value("planning_optimality").toString();
    data->planning_warnings.clear();
    const QJsonArray warnings = metadata->value("planning_warnings").toArray();
    for (const QJsonValue &warning : warnings) {
        if (warning.isString()) {
            data->planning_warnings.append(warning.toString());
        }
    }
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

    data->track_id = firstString(payload.value(), {"track_id"});
    data->cell_code = firstString(payload.value(), {"cell_code", "cell"}, QString::fromStdString(message.waypoint_id()));
    data->animal_name = firstString(payload.value(), {"animal_name", "animal"});
    data->count = firstInt(payload.value(), {"count"}, 0);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HProtocolAdapter::decodeTargetUpdate(
    const TaskEventMessage &message,
    HTargetUpdateData *data,
    QString *error_message) {
    if (data == nullptr) {
        if (error_message != nullptr) {
            *error_message = "H 题目标更新输出参数为空";
        }
        return false;
    }

    const auto payload = payloadObject(QString::fromStdString(message.payload_json()), error_message);
    if (!payload.has_value()) {
        return false;
    }

    data->track_id = firstString(payload.value(), {"track_id"});
    data->cell_code = firstString(payload.value(), {"cell_code", "cell"}, QString::fromStdString(message.waypoint_id()));
    data->animal_name = firstString(payload.value(), {"animal_name", "animal"});
    data->score = firstDouble(payload.value(), {"score"});
    data->target_offset_x_px = firstInt(payload.value(), {"target_offset_x_px"});
    data->target_offset_y_px = firstInt(payload.value(), {"target_offset_y_px"});
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
