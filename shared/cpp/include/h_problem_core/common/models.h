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
    double touchdown_radius_cm = 0.0;
    double preferred_heading_deg = 45.0;
    double heading_tolerance_deg = 35.0;
};

struct CaseConfig {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    int tick_interval_ms = 100;
    QVector<Animal> animals;
    bool return_to_start = false;
    std::optional<LandingProfile> landing;
};

struct MissionPlan {
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

using AckResult = competition::AckResult;
using CommandState = competition::CommandState;

} // namespace hcore
