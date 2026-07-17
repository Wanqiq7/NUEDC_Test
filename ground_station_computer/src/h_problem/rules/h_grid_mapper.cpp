#include "h_problem/rules/h_grid_mapper.h"

#include <QRegularExpression>

#include <stdexcept>

std::optional<QPoint> GridMapper::tryToPoint(const QString &code) {
    static const QRegularExpression pattern(QStringLiteral("\\AA([1-9])B([1-7])\\z"));
    const QRegularExpressionMatch match = pattern.match(code);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return QPoint(match.captured(1).toInt() - 1, match.captured(2).toInt() - 1);
}

QPoint GridMapper::toPoint(const QString &code) {
    const auto point = tryToPoint(code);
    if (!point.has_value()) {
        throw std::invalid_argument("invalid grid code");
    }
    return point.value();
}

QString GridMapper::toCode(const QPoint &point) {
    return QString("A%1B%2").arg(point.x() + 1).arg(point.y() + 1);
}
