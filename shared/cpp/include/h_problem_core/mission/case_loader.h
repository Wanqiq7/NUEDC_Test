#pragma once

#include "h_problem_core/common/models.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace hcore {

std::optional<CaseConfig> caseFromJsonObject(const nlohmann::json &object, std::string *error_message = nullptr);
std::optional<CaseConfig> loadCase(const std::filesystem::path &path, std::string *error_message = nullptr);

} // namespace hcore
