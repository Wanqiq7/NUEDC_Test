#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

struct RouteSegmentVisual {
    QString from_cell;
    QString to_cell;
    bool is_repeated = false;
    int repeat_count = 1;
    QString badge_text;
    int pass_index = 0;
    int total_passes = 1;
    qreal lane_offset = 0.0;
    QColor color;
};

class RouteVisualizer {
public:
    static QList<RouteSegmentVisual> buildSegmentVisuals(const QStringList &route);

private:
    static QColor colorForPass(bool is_repeated);
    static QString normalizedEdgeKey(const QString &from_cell, const QString &to_cell);
};
