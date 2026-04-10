#include "grid_mapper.h"
#include "no_fly_zone_rules.h"

#include <algorithm>
#include <exception>
#include <QSet>
#include <QVector>

NoFlyZoneRules::ValidationResult NoFlyZoneRules::validateSelection(const QStringList &cells, const QString &start_cell) {
    if (cells.size() != 3) {
        return {false, QStringLiteral("必须选择3格")};
    }
    if (cells.contains(start_cell)) {
        return {false, QStringLiteral("不能包含起飞格")};
    }
    QSet<QString> unique_cells;
    for (const QString &cell : cells) {
        unique_cells.insert(cell);
    }
    if (unique_cells.size() != cells.size()) {
        return {false, QStringLiteral("不能包含重复格")};
    }

    QVector<QPoint> points;
    points.reserve(cells.size());
    for (const QString &cell : cells) {
        try {
            points.append(GridMapper::toPoint(cell));
        } catch (const std::exception &) {
            return {false, QStringLiteral("无效的格点选区")};
        } catch (...) {
            return {false, QStringLiteral("无效的格点选区")};
        }
    }

    const bool same_row = (points[0].y() == points[1].y() && points[1].y() == points[2].y());
    const bool same_column = (points[0].x() == points[1].x() && points[1].x() == points[2].x());
    if (!same_row && !same_column) {
        return {false, QStringLiteral("必须横向或纵向连续三格")};
    }

    if (same_row) {
        std::sort(points.begin(), points.end(), [](const QPoint &a, const QPoint &b) {
            return a.x() < b.x();
        });
        for (int i = 1; i < points.size(); ++i) {
            if (points[i].x() - points[i - 1].x() != 1) {
                return {false, QStringLiteral("选区必须连续三格")};
            }
        }
        return {true, QStringLiteral("横向连续三格")};
    }

    std::sort(points.begin(), points.end(), [](const QPoint &a, const QPoint &b) {
        return a.y() < b.y();
    });
    for (int i = 1; i < points.size(); ++i) {
        if (points[i].y() - points[i - 1].y() != 1) {
            return {false, QStringLiteral("选区必须连续三格")};
        }
    }
    return {true, QStringLiteral("纵向连续三格")};
}
