#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace competition {

bool requiredString(const QJsonObject &object, const char *key, QString *output, QString *error_message = nullptr);
bool writeJsonObject(const QJsonObject &object, const QString &output_path, QString *error_message = nullptr);
std::optional<QJsonObject> readJsonObject(const QString &path, const QString &label, QString *error_message = nullptr);

} // namespace competition
