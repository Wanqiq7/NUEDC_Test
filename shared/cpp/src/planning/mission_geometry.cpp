#include "h_problem_core/planning/mission_geometry.h"

#include <QtMath>

namespace hcore {

QString encodeCell(int x_index, int y_index) {
    return QString("A%1B%2").arg(x_index + 1).arg(y_index + 1);
}

std::optional<QPoint> decodeCell(const QString &cell_code, QString *error_message) {
    const int a_index = cell_code.indexOf('A');
    const int b_index = cell_code.indexOf('B');
    if (a_index < 0 || b_index <= a_index + 1 || b_index == cell_code.size() - 1) {
        if (error_message != nullptr) {
            *error_message = "invalid cell code";
        }
        return std::nullopt;
    }

    bool x_ok = false;
    bool y_ok = false;
    const int x_index = cell_code.mid(a_index + 1, b_index - a_index - 1).toInt(&x_ok) - 1;
    const int y_index = cell_code.mid(b_index + 1).toInt(&y_ok) - 1;
    if (!x_ok || !y_ok || x_index < 0 || y_index < 0) {
        if (error_message != nullptr) {
            *error_message = "invalid cell code";
        }
        return std::nullopt;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return QPoint{x_index, y_index};
}

PointCm cellCenterCm(int x_index, int y_index, int height) {
    return {
        FieldMarginCm + ((x_index + 0.5) * CellSizeCm),
        FieldMarginCm + (((height - y_index) - 0.5) * CellSizeCm),
    };
}

std::optional<PointCm> cellCodeCenterCm(const QString &cell_code, int height, QString *error_message) {
    const auto decoded = decodeCell(cell_code, error_message);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    return cellCenterCm(decoded->x(), decoded->y(), height);
}

double euclideanDistanceCm(const PointCm &from_point, const PointCm &to_point) {
    return std::hypot(to_point.x_cm - from_point.x_cm, to_point.y_cm - from_point.y_cm);
}

double headingDegrees(const PointCm &from_point, const PointCm &to_point) {
    return qRadiansToDegrees(std::atan2(to_point.y_cm - from_point.y_cm, to_point.x_cm - from_point.x_cm));
}

double computeDescentRunCm(double cruise_height_cm, double descent_angle_deg) {
    return cruise_height_cm / std::tan(qDegreesToRadians(descent_angle_deg));
}

QPair<double, double> computeDescentRunBoundsCm(double cruise_height_cm, double descent_angle_deg, double angle_tolerance_deg) {
    return {
        computeDescentRunCm(cruise_height_cm, descent_angle_deg + angle_tolerance_deg),
        computeDescentRunCm(cruise_height_cm, descent_angle_deg - angle_tolerance_deg),
    };
}

namespace {

double normalizeHeadingDifferenceDeg(double delta_deg) {
    const double normalized = std::fmod(delta_deg + 180.0 + 360.0, 360.0) - 180.0;
    return std::abs(normalized);
}

}

QSet<QString> terminalCellsForLanding(int width, int height, const QSet<QString> &no_fly_cells, const LandingProfile &landing_profile) {
    const auto bounds = computeDescentRunBoundsCm(landing_profile.cruise_height_cm, landing_profile.descent_angle_deg);
    QSet<QString> candidates;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell_code = encodeCell(x_index, y_index);
            if (no_fly_cells.contains(cell_code)) {
                continue;
            }
            const PointCm center = cellCenterCm(x_index, y_index, height);
            const double distance_cm = euclideanDistanceCm(center, landing_profile.takeoff_anchor_cm);
            if (distance_cm < bounds.first || distance_cm > bounds.second) {
                continue;
            }
            const double approach_heading_deg = headingDegrees(center, landing_profile.takeoff_anchor_cm);
            if (normalizeHeadingDifferenceDeg(approach_heading_deg - landing_profile.preferred_heading_deg) > landing_profile.heading_tolerance_deg) {
                continue;
            }
            candidates.insert(cell_code);
        }
    }
    return candidates;
}

} // namespace hcore
