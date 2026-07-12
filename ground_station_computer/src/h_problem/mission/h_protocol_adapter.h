#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include "competition_core/task/models.h"
#include "messages.pb.h"

struct HGridConfigData {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    QStringList route;
    QString terminal_cell;
    bool landing_enabled = false;
    double descent_angle_deg = 0.0;
    double takeoff_anchor_x_cm = 0.0;
    double takeoff_anchor_y_cm = 0.0;
    double touchdown_x_cm = 0.0;
    double touchdown_y_cm = 0.0;
    double descent_run_cm = 0.0;
    double descent_heading_deg = 0.0;
    double estimated_mission_time_s = 0.0;
    QString planning_optimality;
    QStringList planning_warnings;
};

struct HTelemetryData {
    QString current_cell;
    int step_index = 0;
    int visited_cells = 0;
};

struct HDetectionData {
    QString track_id;
    QString cell_code;
    QString animal_name;
    int count = 0;
};

struct HTargetUpdateData {
    QString track_id;
    QString cell_code;
    QString animal_name;
    double score = 0.0;
    int target_offset_x_px = 0;
    int target_offset_y_px = 0;
};

struct HSummaryData {
    QMap<QString, int> totals;
    int visited_cells = 0;
};

class HProtocolAdapter {
public:
    static bool validateTaskPlan(const competition::TaskPlan &plan, QString *error_message = nullptr);
    static bool decodeTaskPlan(const competition::TaskPlan &plan, HGridConfigData *data, QString *error_message = nullptr);
    static bool decodeTelemetry(const TaskEventMessage &message, HTelemetryData *data, QString *error_message = nullptr);
    static bool decodeDetection(const TaskEventMessage &message, HDetectionData *data, QString *error_message = nullptr);
    static bool decodeTargetUpdate(const TaskEventMessage &message, HTargetUpdateData *data, QString *error_message = nullptr);
    static bool decodeSummary(const TaskSummaryMessage &message, HSummaryData *data, QString *error_message = nullptr);
};
