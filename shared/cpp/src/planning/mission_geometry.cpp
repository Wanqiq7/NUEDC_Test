#include "h_problem_core/planning/mission_geometry.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <tuple>

namespace hcore {

namespace {

constexpr double Pi = 3.141592653589793238462643383279502884;

bool parseQStringCompatibleInt(const char *begin, const char *end, int *value) {
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    if (begin == end) {
        return false;
    }
    if (*begin == '+') {
        ++begin;
        if (begin == end) {
            return false;
        }
    }
    const auto result = std::from_chars(begin, end, *value);
    return result.ec == std::errc{} && result.ptr == end;
}

}

std::string encodeCell(int x_index, int y_index) {
    return "A" + std::to_string(x_index + 1) + "B" + std::to_string(y_index + 1);
}

std::optional<GridPoint> decodeCell(const std::string &cell_code, std::string *error_message) {
    const std::size_t a_index = cell_code.find('A');
    const std::size_t b_index = cell_code.find('B');
    if (a_index == std::string::npos || b_index <= a_index + 1 || b_index == cell_code.size() - 1) {
        if (error_message != nullptr) {
            *error_message = "invalid cell code";
        }
        return std::nullopt;
    }

    int x_value = 0;
    int y_value = 0;
    const char *x_begin = cell_code.data() + a_index + 1;
    const char *x_end = cell_code.data() + b_index;
    const char *y_begin = x_end + 1;
    const char *y_end = cell_code.data() + cell_code.size();
    const bool x_ok = parseQStringCompatibleInt(x_begin, x_end, &x_value);
    const bool y_ok = parseQStringCompatibleInt(y_begin, y_end, &y_value);
    const int x_index = x_value - 1;
    const int y_index = y_value - 1;
    if (!x_ok || !y_ok || x_index < 0 || y_index < 0) {
        if (error_message != nullptr) {
            *error_message = "invalid cell code";
        }
        return std::nullopt;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return GridPoint{x_index, y_index};
}

PointCm cellCenterCm(int x_index, int y_index, int height) {
    return {
        FieldMarginCm + ((x_index + 0.5) * CellSizeCm),
        FieldMarginCm + (((height - y_index) - 0.5) * CellSizeCm),
    };
}

std::optional<PointCm> cellCodeCenterCm(const std::string &cell_code, int height, std::string *error_message) {
    const auto decoded = decodeCell(cell_code, error_message);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    return cellCenterCm(decoded->x, decoded->y, height);
}

MissionPointM fieldPointToMissionMeters(const PointCm &point_cm) {
    return {
        (350.0 - point_cm.y_cm) / 100.0,
        (450.0 - point_cm.x_cm) / 100.0,
    };
}

double euclideanDistanceCm(const PointCm &from_point, const PointCm &to_point) {
    return std::hypot(to_point.x_cm - from_point.x_cm, to_point.y_cm - from_point.y_cm);
}

double headingDegrees(const PointCm &from_point, const PointCm &to_point) {
    return ((std::atan2(to_point.y_cm - from_point.y_cm, to_point.x_cm - from_point.x_cm) * 180.0 / Pi));
}

double computeDescentRunCm(double cruise_height_cm, double descent_angle_deg) {
    return cruise_height_cm / std::tan(((descent_angle_deg) * Pi / 180.0));
}

std::pair<double, double> computeDescentRunBoundsCm(double cruise_height_cm, double descent_angle_deg, double angle_tolerance_deg) {
    return {
        computeDescentRunCm(cruise_height_cm, descent_angle_deg + angle_tolerance_deg),
        computeDescentRunCm(cruise_height_cm, descent_angle_deg - angle_tolerance_deg),
    };
}

namespace {

constexpr double GeometryEpsilon = 1e-8;
constexpr double LandingAngleSampleStepDeg = 0.25;

double normalizeHeadingDifferenceDeg(double delta_deg) {
    const double normalized = std::fmod(delta_deg + 180.0 + 360.0, 360.0) - 180.0;
    return std::abs(normalized);
}

bool pointWithinCellBounds(int x_index, int y_index, int width, int height) {
    return x_index >= 0 && x_index < width && y_index >= 0 && y_index < height;
}

bool segmentIntersectsClosedRectangle(
    const PointCm &from_point,
    const PointCm &to_point,
    double min_x,
    double max_x,
    double min_y,
    double max_y) {
    const double delta_x = to_point.x_cm - from_point.x_cm;
    const double delta_y = to_point.y_cm - from_point.y_cm;
    double enter_t = 0.0;
    double leave_t = 1.0;

    const auto clip_axis = [&](double start, double delta, double minimum, double maximum) {
        if (std::abs(delta) <= GeometryEpsilon) {
            return start >= minimum - GeometryEpsilon && start <= maximum + GeometryEpsilon;
        }
        double first = (minimum - start) / delta;
        double second = (maximum - start) / delta;
        if (first > second) {
            std::swap(first, second);
        }
        enter_t = std::max(enter_t, first);
        leave_t = std::min(leave_t, second);
        return enter_t <= leave_t + GeometryEpsilon;
    };

    return clip_axis(from_point.x_cm, delta_x, min_x, max_x)
        && clip_axis(from_point.y_cm, delta_y, min_y, max_y);
}

bool angleIsPermitted(double angle_deg, const LandingProfile &landing_profile) {
    return normalizeHeadingDifferenceDeg(angle_deg - landing_profile.preferred_heading_deg)
        <= std::min(180.0, std::max(0.0, landing_profile.heading_tolerance_deg)) + GeometryEpsilon;
}

void appendCandidateAngle(std::vector<double> *angles, double angle_deg, const LandingProfile &landing_profile) {
    if (!angleIsPermitted(angle_deg, landing_profile)) {
        return;
    }
    for (const double existing : *angles) {
        if (normalizeHeadingDifferenceDeg(existing - angle_deg) <= GeometryEpsilon) {
            return;
        }
    }
    angles->push_back(angle_deg);
}

}

bool descentCorridorIsClear(
    int width,
    int height,
    const PointCm &descent_start_cm,
    const PointCm &touchdown_point_cm,
    const CellSet &no_fly_cells) {
    for (const std::string &cell : no_fly_cells) {
        const auto decoded = decodeCell(cell);
        if (!decoded.has_value() || !pointWithinCellBounds(decoded->x, decoded->y, width, height)) {
            continue;
        }
        const double min_x = FieldMarginCm + (decoded->x * CellSizeCm);
        const double max_x = min_x + CellSizeCm;
        const double min_y = FieldMarginCm + ((height - decoded->y - 1) * CellSizeCm);
        const double max_y = min_y + CellSizeCm;
        if (segmentIntersectsClosedRectangle(
                descent_start_cm,
                touchdown_point_cm,
                min_x,
                max_x,
                min_y,
                max_y)) {
            return false;
        }
    }
    return true;
}

std::optional<LandingApproach> landingApproachForTerminal(
    int width,
    int height,
    const std::string &terminal_cell,
    const CellSet &no_fly_cells,
    const LandingProfile &landing_profile) {
    const auto terminal = decodeCell(terminal_cell);
    if (!terminal.has_value()
        || !pointWithinCellBounds(terminal->x, terminal->y, width, height)
        || no_fly_cells.count(terminal_cell) != 0
        || landing_profile.cruise_height_cm <= 0.0
        || landing_profile.descent_angle_deg <= 0.0
        || landing_profile.descent_angle_tolerance_deg < 0.0
        || landing_profile.touchdown_radius_cm < 0.0) {
        return std::nullopt;
    }

    const auto bounds = computeDescentRunBoundsCm(
        landing_profile.cruise_height_cm,
        landing_profile.descent_angle_deg,
        landing_profile.descent_angle_tolerance_deg);
    const double minimum_run_cm = bounds.first;
    const double maximum_run_cm = bounds.second;
    if (!std::isfinite(minimum_run_cm)
        || !std::isfinite(maximum_run_cm)
        || minimum_run_cm < 0.0
        || maximum_run_cm < minimum_run_cm) {
        return std::nullopt;
    }

    const PointCm terminal_center = cellCenterCm(terminal->x, terminal->y, height);
    std::vector<double> angles;
    const double heading_tolerance_deg = std::min(180.0, std::max(0.0, landing_profile.heading_tolerance_deg));
    const int sample_count = std::max(
        1,
        static_cast<int>(std::ceil((2.0 * heading_tolerance_deg) / LandingAngleSampleStepDeg)));
    for (int sample_index = 0; sample_index <= sample_count; ++sample_index) {
        const double fraction = static_cast<double>(sample_index) / static_cast<double>(sample_count);
        appendCandidateAngle(
            &angles,
            landing_profile.preferred_heading_deg - heading_tolerance_deg
                + (fraction * 2.0 * heading_tolerance_deg),
            landing_profile);
    }
    appendCandidateAngle(
        &angles,
        headingDegrees(terminal_center, landing_profile.takeoff_anchor_cm),
        landing_profile);

    for (const std::string &cell : no_fly_cells) {
        const auto blocked = decodeCell(cell);
        if (!blocked.has_value() || !pointWithinCellBounds(blocked->x, blocked->y, width, height)) {
            continue;
        }
        const double min_x = FieldMarginCm + (blocked->x * CellSizeCm);
        const double max_x = min_x + CellSizeCm;
        const double min_y = FieldMarginCm + ((height - blocked->y - 1) * CellSizeCm);
        const double max_y = min_y + CellSizeCm;
        for (const PointCm &corner : std::vector<PointCm>{{min_x, min_y}, {min_x, max_y}, {max_x, min_y}, {max_x, max_y}}) {
            const double corner_heading_deg = headingDegrees(terminal_center, corner);
            appendCandidateAngle(&angles, corner_heading_deg - 1e-5, landing_profile);
            appendCandidateAngle(&angles, corner_heading_deg + 1e-5, landing_profile);
        }
    }

    std::optional<LandingApproach> best_approach;
    const double anchor_delta_x = landing_profile.takeoff_anchor_cm.x_cm - terminal_center.x_cm;
    const double anchor_delta_y = landing_profile.takeoff_anchor_cm.y_cm - terminal_center.y_cm;
    const double anchor_distance_squared = (anchor_delta_x * anchor_delta_x) + (anchor_delta_y * anchor_delta_y);
    const double radius_squared = landing_profile.touchdown_radius_cm * landing_profile.touchdown_radius_cm;

    for (const double angle_deg : angles) {
        const double angle_rad = ((angle_deg) * Pi / 180.0);
        const double direction_x = std::cos(angle_rad);
        const double direction_y = std::sin(angle_rad);
        const double projection = (anchor_delta_x * direction_x) + (anchor_delta_y * direction_y);
        const double discriminant = (projection * projection) - anchor_distance_squared + radius_squared;
        if (discriminant < -GeometryEpsilon) {
            continue;
        }
        const double root = std::sqrt(std::max(0.0, discriminant));
        const double permitted_minimum = std::max(minimum_run_cm, projection - root);
        const double permitted_maximum = std::min(maximum_run_cm, projection + root);
        if (permitted_minimum > permitted_maximum + GeometryEpsilon) {
            continue;
        }

        const PointCm touchdown_point{
            terminal_center.x_cm + (permitted_minimum * direction_x),
            terminal_center.y_cm + (permitted_minimum * direction_y),
        };
        if (!descentCorridorIsClear(width, height, terminal_center, touchdown_point, no_fly_cells)) {
            continue;
        }

        const LandingApproach approach{
            touchdown_point,
            permitted_minimum,
            std::hypot(permitted_minimum, landing_profile.cruise_height_cm),
        };
        if (!best_approach.has_value()
            || approach.descent_distance_cm < best_approach->descent_distance_cm - GeometryEpsilon
            || (std::abs(approach.descent_distance_cm - best_approach->descent_distance_cm) <= GeometryEpsilon
                && std::tie(approach.touchdown_point_cm.x_cm, approach.touchdown_point_cm.y_cm)
                    < std::tie(best_approach->touchdown_point_cm.x_cm, best_approach->touchdown_point_cm.y_cm))) {
            best_approach = approach;
        }
    }

    return best_approach;
}

CellSet terminalCellsForLanding(int width, int height, const CellSet &no_fly_cells, const LandingProfile &landing_profile) {
    CellSet candidates;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const std::string cell_code = encodeCell(x_index, y_index);
            if (no_fly_cells.count(cell_code) != 0) {
                continue;
            }
            if (landingApproachForTerminal(width, height, cell_code, no_fly_cells, landing_profile).has_value()) {
                candidates.insert(cell_code);
            }
        }
    }
    return candidates;
}

} // namespace hcore
