#pragma once

#include <QString>
#include <QStringList>

class RouteValidator {
public:
    static bool isAdjacent(const QString &from_cell, const QString &to_cell);
    static bool validateRoute(const QStringList &route, const QStringList &no_fly_cells, QString *error_message = nullptr);
    static bool validateClosedRoute(
        const QStringList &route,
        const QStringList &no_fly_cells,
        const QString &start_cell,
        QString *error_message = nullptr);
};
