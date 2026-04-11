#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

struct MissionPlanData {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    QStringList route;
    QString terminal_cell;
    bool landing_enabled = false;
    std::optional<double> descent_angle_deg;
    std::optional<double> takeoff_anchor_x_cm;
    std::optional<double> takeoff_anchor_y_cm;
};

struct MissionPlanResult {
    bool ok = false;
    MissionPlanData plan;
    QString error_message;
};

class MissionPlanBridge {
public:
    static MissionPlanResult parsePlannerOutput(const QByteArray &stdout_bytes);
    MissionPlanResult generatePlan(const QString &case_path, const QStringList &no_fly_cells) const;
};
