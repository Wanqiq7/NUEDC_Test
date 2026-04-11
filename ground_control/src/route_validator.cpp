#include "route_validator.h"

#include "grid_mapper.h"

bool RouteValidator::isAdjacent(const QString &from_cell, const QString &to_cell) {
    const QPoint from_point = GridMapper::toPoint(from_cell);
    const QPoint to_point = GridMapper::toPoint(to_cell);
    const int manhattan_distance = qAbs(from_point.x() - to_point.x()) + qAbs(from_point.y() - to_point.y());
    return manhattan_distance == 1;
}

bool RouteValidator::validateRoute(
    const QStringList &route,
    const QStringList &no_fly_cells,
    QString *error_message) {
    for (const QString &cell : route) {
        if (no_fly_cells.contains(cell)) {
            if (error_message != nullptr) {
                *error_message = QString("route enters no-fly cell: %1").arg(cell);
            }
            return false;
        }
    }

    for (int index = 1; index < route.size(); ++index) {
        if (!isAdjacent(route[index - 1], route[index])) {
            if (error_message != nullptr) {
                *error_message = QString("illegal jump: %1 -> %2").arg(route[index - 1], route[index]);
            }
            return false;
        }
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool RouteValidator::validateClosedRoute(
    const QStringList &route,
    const QStringList &no_fly_cells,
    const QString &start_cell,
    QString *error_message) {
    if (route.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "route is empty";
        }
        return false;
    }

    if (route.first() != start_cell) {
        if (error_message != nullptr) {
            *error_message = QString("closed route must start at %1").arg(start_cell);
        }
        return false;
    }

    if (route.last() != start_cell) {
        if (error_message != nullptr) {
            *error_message = QString("closed route must end at %1").arg(start_cell);
        }
        return false;
    }

    return validateRoute(route, no_fly_cells, error_message);
}
