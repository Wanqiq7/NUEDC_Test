#include "grid_mapper.h"

#include <stdexcept>

QPoint GridMapper::toPoint(const QString &code) {
    const int a_index = code.indexOf('A');
    const int b_index = code.indexOf('B');
    if (a_index == -1 || b_index == -1 || b_index <= a_index + 1) {
        throw std::invalid_argument("invalid grid code");
    }

    bool x_ok = false;
    bool y_ok = false;
    const int x = code.mid(a_index + 1, b_index - a_index - 1).toInt(&x_ok) - 1;
    const int y = code.mid(b_index + 1).toInt(&y_ok) - 1;
    if (!x_ok || !y_ok) {
        throw std::invalid_argument("invalid grid code");
    }
    return {x, y};
}

QString GridMapper::toCode(const QPoint &point) {
    return QString("A%1B%2").arg(point.x() + 1).arg(point.y() + 1);
}
