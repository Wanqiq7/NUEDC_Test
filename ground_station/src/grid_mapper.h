#pragma once

#include <QPoint>
#include <QString>

class GridMapper {
public:
    static QPoint toPoint(const QString &code);
    static QString toCode(const QPoint &point);
};
