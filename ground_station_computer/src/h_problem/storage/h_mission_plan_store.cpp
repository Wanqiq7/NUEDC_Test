#include "h_problem/storage/h_mission_plan_store.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

MissionPlanStore::MissionPlanStore(QString output_path)
    : output_path_(std::move(output_path)) {}

bool MissionPlanStore::save(const MissionPlanData &plan, QString *error_message) const {
    QJsonObject payload;
    payload["message_type"] = "config";
    payload["case_id"] = plan.case_id;
    payload["start_cell"] = plan.start_cell;
    payload["terminal_cell"] = plan.terminal_cell;
    payload["landing_enabled"] = plan.landing_enabled;
    payload["descent_angle_deg"] = plan.descent_angle_deg.has_value()
        ? QJsonValue(plan.descent_angle_deg.value())
        : QJsonValue::Null;
    payload["takeoff_anchor_x_cm"] = plan.takeoff_anchor_x_cm.has_value()
        ? QJsonValue(plan.takeoff_anchor_x_cm.value())
        : QJsonValue::Null;
    payload["takeoff_anchor_y_cm"] = plan.takeoff_anchor_y_cm.has_value()
        ? QJsonValue(plan.takeoff_anchor_y_cm.value())
        : QJsonValue::Null;

    QJsonArray no_fly_cells_json;
    for (const QString &cell_code : plan.no_fly_cells) {
        no_fly_cells_json.append(cell_code);
    }
    payload["no_fly_cells"] = no_fly_cells_json;

    QJsonArray route_json;
    for (const QString &cell_code : plan.route) {
        route_json.append(cell_code);
    }
    payload["route"] = route_json;

    const QFileInfo output_file_info(output_path_);
    QDir().mkpath(output_file_info.absolutePath());

    QSaveFile output_file(output_file_info.filePath());
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    const QByteArray document_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (output_file.write(document_bytes) != document_bytes.size()) {
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

    return true;
}
