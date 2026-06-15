#pragma once

#include "h_problem_core/common/models.h"

#include <QJsonObject>

namespace hcore {

std::optional<CaseConfig> caseFromJsonObject(const QJsonObject &object, QString *error_message = nullptr);
std::optional<CaseConfig> loadCase(const QString &path, QString *error_message = nullptr);

} // namespace hcore
