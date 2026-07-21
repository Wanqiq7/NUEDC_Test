#pragma once

#include "h_problem_core/common/models.h"

namespace hcore {

constexpr double FieldMarginCm = 25.0;
constexpr double CellSizeCm = 50.0;

struct MissionPointM {
    double x_m = 0.0;
    double y_m = 0.0;
};

std::string encodeCell(int x_index, int y_index);
std::optional<GridPoint> decodeCell(const std::string &cell_code, std::string *error_message = nullptr);
PointCm cellCenterCm(int x_index, int y_index, int height);
std::optional<PointCm> cellCodeCenterCm(const std::string &cell_code, int height, std::string *error_message = nullptr);
MissionPointM fieldPointToMissionMeters(const PointCm &point_cm);
double euclideanDistanceCm(const PointCm &from_point, const PointCm &to_point);
double headingDegrees(const PointCm &from_point, const PointCm &to_point);
double computeDescentRunCm(double cruise_height_cm, double descent_angle_deg);
std::pair<double, double> computeDescentRunBoundsCm(double cruise_height_cm, double descent_angle_deg, double angle_tolerance_deg = 5.0);

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
    const CellSet &no_fly_cells);
std::optional<LandingApproach> landingApproachForTerminal(
    int width,
    int height,
    const std::string &terminal_cell,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile);
CellSet terminalCellsForLanding(int width, int height, const CellSet &no_fly_cells, const LandingProfile &landing_profile);

} // namespace hcore
