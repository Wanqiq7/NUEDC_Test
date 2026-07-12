#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

constexpr double FieldMarginCm = 25.0;
constexpr double CellSizeCm = 50.0;

QString encodeCell(int x_index, int y_index);
std::optional<QPoint> decodeCell(const QString &cell_code, QString *error_message = nullptr);
PointCm cellCenterCm(int x_index, int y_index, int height);
std::optional<PointCm> cellCodeCenterCm(const QString &cell_code, int height, QString *error_message = nullptr);
double euclideanDistanceCm(const PointCm &from_point, const PointCm &to_point);
double headingDegrees(const PointCm &from_point, const PointCm &to_point);
double computeDescentRunCm(double cruise_height_cm, double descent_angle_deg);
QPair<double, double> computeDescentRunBoundsCm(double cruise_height_cm, double descent_angle_deg, double angle_tolerance_deg = 5.0);

struct LandingApproach {
    PointCm touchdown_point_cm;
    double horizontal_run_cm = 0.0;
    double descent_distance_cm = 0.0;
};

bool descentCorridorIsClear(
    int width,
    int height,
    const PointCm &descent_start_cm,
    const PointCm &touchdown_point_cm,
    const QSet<QString> &no_fly_cells);
std::optional<LandingApproach> landingApproachForTerminal(
    int width,
    int height,
    const QString &terminal_cell,
    const QSet<QString> &no_fly_cells,
    const LandingProfile &landing_profile);
QSet<QString> terminalCellsForLanding(int width, int height, const QSet<QString> &no_fly_cells, const LandingProfile &landing_profile);

} // namespace hcore
