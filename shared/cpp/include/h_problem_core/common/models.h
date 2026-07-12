#pragma once

#include "competition_core/task/models.h"

#include <QMap>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace hcore {

struct PointCm {
    double x_cm = 0.0;
    double y_cm = 0.0;
};

struct Animal {
    QString cell;
    QString name;
    quint32 count = 0;
};

struct LandingProfile {
    PointCm takeoff_anchor_cm;
    double cruise_height_cm = 0.0;
    double descent_angle_deg = 0.0;
    double descent_angle_tolerance_deg = 5.0;
    double touchdown_radius_cm = 0.0;
    double preferred_heading_deg = 45.0;
    double heading_tolerance_deg = 35.0;
};

struct MissionTiming {
    // 赛题未规定速度与识别等待时间，默认值必须按实机标定后覆盖。
    double cruise_speed_cm_per_s = 100.0;
    double ascent_speed_cm_per_s = 100.0;
    double descent_speed_cm_per_s = 100.0;
    double takeoff_fixed_time_s = 0.0;
    double landing_fixed_time_s = 0.0;
    double per_cell_dwell_time_s = 0.0;
};

struct CaseConfig {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    int tick_interval_ms = 100;
    QVector<Animal> animals;
    std::optional<LandingProfile> landing;
    MissionTiming mission_timing;
};

using AckResult = competition::AckResult;
using CommandState = competition::CommandState;

} // namespace hcore
