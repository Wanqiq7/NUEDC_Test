#include "h_problem_core/mission/case_loader.h"

#include <cerrno>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>

namespace hcore {
namespace {

bool requireString(const nlohmann::json &object, const char *key, std::string *value, std::string *error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        if (error) *error = std::string("missing or invalid ") + key;
        return false;
    }
    *value = it->get<std::string>();
    return true;
}

bool optionalStringList(const nlohmann::json &object, const char *key, CellList *result, std::string *error) {
    const auto it = object.find(key);
    if (it == object.end()) return true;
    if (!it->is_array()) { if (error) *error = std::string("invalid ") + key; return false; }
    for (const auto &entry : *it) {
        if (!entry.is_string()) { if (error) *error = std::string(key) + " must contain strings"; return false; }
        result->push_back(entry.get<std::string>());
    }
    return true;
}

bool optionalTimingValue(const nlohmann::json &object, const char *key, double *target,
        bool positive, std::string *error) {
    const auto it = object.find(key);
    if (it == object.end()) return true;
    if (!it->is_number()) { if (error) *error = std::string("invalid mission_timing.") + key; return false; }
    const double number = it->get<double>();
    if (!std::isfinite(number) || (positive ? number <= 0.0 : number < 0.0)) {
        if (error) *error = std::string("invalid mission_timing.") + key;
        return false;
    }
    *target = number;
    return true;
}

bool finiteNumber(const nlohmann::json &object, const char *key, double *result) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) return false;
    *result = it->get<double>();
    return std::isfinite(*result);
}

bool optionalFiniteNumber(const nlohmann::json &object, const char *key, double *result,
        std::string *error) {
    const auto it = object.find(key);
    if (it == object.end()) return true;
    if (!it->is_number()) {
        if (error) *error = std::string("invalid landing.") + key;
        return false;
    }
    const double value = it->get<double>();
    if (!std::isfinite(value)) {
        if (error) *error = std::string("invalid landing.") + key;
        return false;
    }
    *result = value;
    return true;
}

int jsonIntOrDefault(const nlohmann::json &value, int default_value) {
    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        return number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            ? static_cast<int>(number)
            : default_value;
    }
    if (value.is_number_integer()) {
        const std::int64_t number = value.get<std::int64_t>();
        return number >= std::numeric_limits<int>::min()
                && number <= std::numeric_limits<int>::max()
            ? static_cast<int>(number)
            : default_value;
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (std::isfinite(number)
            && number >= std::numeric_limits<int>::min()
            && number <= std::numeric_limits<int>::max()
            && std::trunc(number) == number) {
            return static_cast<int>(number);
        }
    }
    return default_value;
}

std::string filesystemErrorMessage(const std::error_code &error_code) {
    return error_code ? error_code.message() : std::make_error_code(std::errc::io_error).message();
}

} // namespace

std::optional<CaseConfig> caseFromJsonObject(const nlohmann::json &object, std::string *error) {
    if (!object.is_object()) { if (error) *error = "case JSON must be an object"; return std::nullopt; }
    CaseConfig config;
    if (!requireString(object, "case_id", &config.case_id, error)
        || !requireString(object, "start_cell", &config.start_cell, error)
        || !optionalStringList(object, "no_fly_cells", &config.no_fly_cells, error)) return std::nullopt;
    if (const auto it = object.find("tick_interval_ms"); it != object.end()) {
        config.tick_interval_ms = jsonIntOrDefault(*it, config.tick_interval_ms);
    }
    const auto return_to_start = object.find("return_to_start");
    if (return_to_start != object.end() && return_to_start->is_boolean()
        && return_to_start->get<bool>()) {
        if (error) *error = "return_to_start is unsupported; H missions use a landing-compatible open route";
        return std::nullopt;
    }
    if (const auto it = object.find("mission_timing"); it != object.end()) {
        if (!it->is_object()) { if (error) *error = "invalid mission_timing"; return std::nullopt; }
        if (!optionalTimingValue(*it, "cruise_speed_cm_per_s", &config.mission_timing.cruise_speed_cm_per_s, true, error)
            || !optionalTimingValue(*it, "ascent_speed_cm_per_s", &config.mission_timing.ascent_speed_cm_per_s, true, error)
            || !optionalTimingValue(*it, "descent_speed_cm_per_s", &config.mission_timing.descent_speed_cm_per_s, true, error)
            || !optionalTimingValue(*it, "takeoff_fixed_time_s", &config.mission_timing.takeoff_fixed_time_s, false, error)
            || !optionalTimingValue(*it, "landing_fixed_time_s", &config.mission_timing.landing_fixed_time_s, false, error)
            || !optionalTimingValue(*it, "per_cell_dwell_time_s", &config.mission_timing.per_cell_dwell_time_s, false, error)) return std::nullopt;
    }
    if (const auto it = object.find("animals"); it != object.end()) {
        if (!it->is_array()) { if (error) *error = "animals must contain objects"; return std::nullopt; }
        for (const auto &entry : *it) {
            if (!entry.is_object()) { if (error) *error = "animals must contain objects"; return std::nullopt; }
            Animal animal;
            if (!requireString(entry, "cell", &animal.cell, error) || !requireString(entry, "name", &animal.name, error)) return std::nullopt;
            const auto count = entry.find("count");
            if (count != entry.end()) {
                if (!count->is_number_unsigned() && !count->is_number_integer()) {
                    if (error) *error = "invalid animal count";
                    return std::nullopt;
                }
                const std::uint64_t value = count->is_number_unsigned()
                    ? count->get<std::uint64_t>()
                    : count->get<std::int64_t>() < 0
                        ? std::numeric_limits<std::uint64_t>::max()
                        : static_cast<std::uint64_t>(count->get<std::int64_t>());
                if (value > std::numeric_limits<std::uint32_t>::max()) {
                    if (error) *error = "invalid animal count";
                    return std::nullopt;
                }
                animal.count = static_cast<std::uint32_t>(value);
            }
            config.animals.push_back(animal);
        }
    }
    if (const auto it = object.find("landing"); it != object.end()) {
        if (!it->is_object()) { if (error) *error = "invalid landing"; return std::nullopt; }
        const auto anchor = it->find("takeoff_anchor_cm");
        if (anchor == it->end() || !anchor->is_array() || anchor->size() < 2
            || !(*anchor)[0].is_number() || !(*anchor)[1].is_number()) {
            if (error) *error = "missing or invalid takeoff_anchor_cm"; return std::nullopt;
        }
        LandingProfile landing;
        landing.takeoff_anchor_cm = {(*anchor)[0].get<double>(), (*anchor)[1].get<double>()};
        if (!std::isfinite(landing.takeoff_anchor_cm.x_cm)
            || !std::isfinite(landing.takeoff_anchor_cm.y_cm)) {
            if (error) *error = "missing or invalid takeoff_anchor_cm";
            return std::nullopt;
        }
        if (!finiteNumber(*it, "cruise_height_cm", &landing.cruise_height_cm)
            || !finiteNumber(*it, "descent_angle_deg", &landing.descent_angle_deg)
            || !finiteNumber(*it, "touchdown_radius_cm", &landing.touchdown_radius_cm)) {
            if (error) *error = "missing or invalid landing value"; return std::nullopt;
        }
        if (!optionalFiniteNumber(*it, "descent_angle_tolerance_deg", &landing.descent_angle_tolerance_deg, error)
            || !optionalFiniteNumber(*it, "preferred_heading_deg", &landing.preferred_heading_deg, error)
            || !optionalFiniteNumber(*it, "heading_tolerance_deg", &landing.heading_tolerance_deg, error)) {
            return std::nullopt;
        }
        config.landing = landing;
    }
    if (error) error->clear();
    return config;
}

std::optional<CaseConfig> loadCase(const std::filesystem::path &path, std::string *error) {
    std::error_code status_error;
    const std::filesystem::file_status status = std::filesystem::status(path, status_error);
    if (status_error) {
        if (error) *error = filesystemErrorMessage(status_error);
        return std::nullopt;
    }
    if (!std::filesystem::exists(status)) {
        if (error) *error = std::make_error_code(std::errc::no_such_file_or_directory).message();
        return std::nullopt;
    }
    if (std::filesystem::is_directory(status)) {
        if (error) *error = std::make_error_code(std::errc::is_a_directory).message();
        return std::nullopt;
    }

    errno = 0;
    std::ifstream file(path);
    if (!file) {
        if (error) *error = filesystemErrorMessage({errno, std::generic_category()});
        return std::nullopt;
    }
    try {
        nlohmann::json document;
        file >> document;
        if (!document.is_object()) { if (error) *error = "failed to parse case JSON (document is not an object)"; return std::nullopt; }
        return caseFromJsonObject(document, error);
    } catch (const nlohmann::json::exception &exception) {
        if (error) *error = std::string("failed to parse case JSON (") + exception.what() + ")";
        return std::nullopt;
    } catch (const std::ios_base::failure &exception) {
        if (error) *error = filesystemErrorMessage(exception.code());
        return std::nullopt;
    }
}

} // namespace hcore
