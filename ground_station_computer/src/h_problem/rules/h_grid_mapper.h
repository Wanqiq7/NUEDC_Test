#pragma once

#include <QPoint>
#include <QString>

#include <optional>

class GridMapper {
public:
    static std::optional<QPoint> tryToPoint(const QString &code);
    static QPoint toPoint(const QString &code);
    static QString toCode(const QPoint &point);
};
