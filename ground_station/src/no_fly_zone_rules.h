#pragma once

#include <QString>
#include <QStringList>

class NoFlyZoneRules {
public:
    struct ValidationResult {
        bool is_valid;
        QString message;
    };

    static ValidationResult validateSelection(const QStringList &cells, const QString &start_cell);
};
