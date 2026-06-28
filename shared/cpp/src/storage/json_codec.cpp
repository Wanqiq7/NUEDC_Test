#include "competition_core/storage/json_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace competition {

bool requiredString(const QJsonObject &object, const char *key, QString *output, QString *error_message) {
    if (!object.contains(key) || !object.value(key).isString() || object.value(key).toString().isEmpty()) {
        if (error_message != nullptr) {
            *error_message = QString("missing %1").arg(QString::fromUtf8(key));
        }
        return false;
    }
    *output = object.value(key).toString();
    return true;
}

bool writeJsonObject(const QJsonObject &object, const QString &output_path, QString *error_message) {
    const QFileInfo output_info(output_path);
    QDir().mkpath(output_info.absolutePath());
    QSaveFile output_file(output_info.filePath());
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (output_file.write(bytes) != bytes.size()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        output_file.cancelWriting();
        return false;
    }
    if (!output_file.commit()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

std::optional<QJsonObject> readJsonObject(const QString &path, const QString &label, QString *error_message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = file.errorString();
        }
        return std::nullopt;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error_message != nullptr) {
            *error_message = QString("failed to parse %1 JSON (%2)").arg(label, parse_error.errorString());
        }
        return std::nullopt;
    }
    return document.object();
}

} // namespace competition
