#include "h_problem_core/mission/case_loader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hcore {

namespace {

bool requireString(const QJsonObject &object, const char *key, QString *value, QString *error_message) {
    if (!object.contains(key) || !object.value(key).isString()) {
        if (error_message != nullptr) {
            *error_message = QString("missing or invalid %1").arg(QString::fromUtf8(key));
        }
        return false;
    }
    *value = object.value(key).toString();
    return true;
}

QStringList optionalStringList(const QJsonObject &object, const char *key, QString *error_message) {
    QStringList result;
    if (!object.contains(key)) {
        return result;
    }
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        if (error_message != nullptr) {
            *error_message = QString("invalid %1").arg(QString::fromUtf8(key));
        }
        return {};
    }
    for (const QJsonValue entry : value.toArray()) {
        if (!entry.isString()) {
            if (error_message != nullptr) {
                *error_message = QString("%1 must contain strings").arg(QString::fromUtf8(key));
            }
            return {};
        }
        result.append(entry.toString());
    }
    return result;
}

}

std::optional<CaseConfig> caseFromJsonObject(const QJsonObject &object, QString *error_message) {
    CaseConfig config;
    if (!requireString(object, "case_id", &config.case_id, error_message)
        || !requireString(object, "start_cell", &config.start_cell, error_message)) {
        return std::nullopt;
    }

    QString list_error;
    config.no_fly_cells = optionalStringList(object, "no_fly_cells", &list_error);
    if (!list_error.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = list_error;
        }
        return std::nullopt;
    }

    config.tick_interval_ms = object.value("tick_interval_ms").toInt(100);
    config.return_to_start = object.value("return_to_start").toBool(false);

    const QJsonValue animals_value = object.value("animals");
    if (animals_value.isArray()) {
        for (const QJsonValue entry : animals_value.toArray()) {
            if (!entry.isObject()) {
                if (error_message != nullptr) {
                    *error_message = "animals must contain objects";
                }
                return std::nullopt;
            }
            const QJsonObject animal_object = entry.toObject();
            Animal animal;
            if (!requireString(animal_object, "cell", &animal.cell, error_message)
                || !requireString(animal_object, "name", &animal.name, error_message)) {
                return std::nullopt;
            }
            animal.count = static_cast<quint32>(animal_object.value("count").toInt());
            config.animals.append(animal);
        }
    }

    const QJsonValue landing_value = object.value("landing");
    if (landing_value.isObject()) {
        const QJsonObject landing_object = landing_value.toObject();
        const QJsonValue anchor_value = landing_object.value("takeoff_anchor_cm");
        if (!anchor_value.isArray() || anchor_value.toArray().size() < 2) {
            if (error_message != nullptr) {
                *error_message = "missing or invalid takeoff_anchor_cm";
            }
            return std::nullopt;
        }

        LandingProfile landing;
        landing.takeoff_anchor_cm = {
            anchor_value.toArray().at(0).toDouble(),
            anchor_value.toArray().at(1).toDouble(),
        };
        landing.cruise_height_cm = landing_object.value("cruise_height_cm").toDouble();
        landing.descent_angle_deg = landing_object.value("descent_angle_deg").toDouble();
        landing.touchdown_radius_cm = landing_object.value("touchdown_radius_cm").toDouble();
        landing.preferred_heading_deg = landing_object.value("preferred_heading_deg").toDouble(45.0);
        config.landing = landing;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return config;
}

std::optional<CaseConfig> loadCase(const QString &path, QString *error_message) {
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
            *error_message = QString("failed to parse case JSON (%1)").arg(parse_error.errorString());
        }
        return std::nullopt;
    }
    return caseFromJsonObject(document.object(), error_message);
}

} // namespace hcore
