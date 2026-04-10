#include "route_visualizer.h"

#include <QHash>

QList<RouteSegmentVisual> RouteVisualizer::buildSegmentVisuals(const QStringList &route) {
    QList<RouteSegmentVisual> visuals;
    if (route.size() < 2) {
        return visuals;
    }

    QHash<QString, int> total_pass_counts;
    for (int index = 1; index < route.size(); ++index) {
        total_pass_counts[normalizedEdgeKey(route[index - 1], route[index])] += 1;
    }

    QHash<QString, int> current_pass_counts;
    for (int index = 1; index < route.size(); ++index) {
        const QString edge_key = normalizedEdgeKey(route[index - 1], route[index]);
        const int repeat_count = total_pass_counts.value(edge_key, 1);
        const bool is_repeated = repeat_count > 1;
        const int pass_index = current_pass_counts.value(edge_key, 0);

        RouteSegmentVisual visual;
        visual.from_cell = route[index - 1];
        visual.to_cell = route[index];
        visual.is_repeated = is_repeated;
        visual.repeat_count = repeat_count;
        visual.badge_text = is_repeated ? QString("%1x").arg(repeat_count) : QString();
        visual.pass_index = pass_index;
        visual.total_passes = repeat_count;
        visual.color = colorForPass(is_repeated);
        visuals.append(visual);

        current_pass_counts[edge_key] = pass_index + 1;
    }

    return visuals;
}

QColor RouteVisualizer::colorForPass(bool is_repeated) {
    return is_repeated ? QColor(240, 140, 0) : QColor(47, 111, 237);
}

QString RouteVisualizer::normalizedEdgeKey(const QString &from_cell, const QString &to_cell) {
    return from_cell < to_cell
        ? QString("%1|%2").arg(from_cell, to_cell)
        : QString("%1|%2").arg(to_cell, from_cell);
}
