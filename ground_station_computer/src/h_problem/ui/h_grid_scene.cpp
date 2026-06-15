#include "h_problem/ui/h_grid_scene.h"

#include "h_problem/rules/h_grid_mapper.h"
#include "h_problem/ui/h_route_visualizer.h"
#include "h_problem/rules/h_route_validator.h"

#include <QBrush>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QSet>
#include <QFont>
#include <QDebug>
#include <QPen>
#include <QVariant>

namespace {
constexpr int kColumns = 9;
constexpr int kRows = 7;
constexpr qreal kCellSize = 52.0;
constexpr qreal kFieldMarginCm = 25.0;
constexpr qreal kGridCellCm = 50.0;

QList<qreal> buildArrowOffsets(int repeat_count) {
    QList<qreal> offsets;
    if (repeat_count <= 1) {
        offsets.append(0.0);
        return offsets;
    }

    constexpr qreal kArrowSpacing = 6.0;
    if (repeat_count % 2 == 1) {
        offsets.append(0.0);
    }

    const int side_pairs = repeat_count / 2;
    for (int index = 0; index < side_pairs; ++index) {
        const qreal magnitude = (index + 0.5) * kArrowSpacing;
        offsets.prepend(-magnitude);
        offsets.append(magnitude);
    }
    return offsets;
}

QPointF normalVector(const QLineF &line, qreal lane_offset) {
    if (qFuzzyIsNull(line.length()) || qFuzzyIsNull(lane_offset)) {
        return QPointF(0.0, 0.0);
    }

    const qreal dx = line.dx() / line.length();
    const qreal dy = line.dy() / line.length();
    return QPointF(-dy * lane_offset, dx * lane_offset);
}

qreal arrowOffsetForPass(int pass_index, int repeat_count) {
    const QList<qreal> offsets = buildArrowOffsets(repeat_count);
    if (pass_index < 0 || pass_index >= offsets.size()) {
        return 0.0;
    }
    return offsets[pass_index];
}

QPolygonF buildArrowHead(const QLineF &line, qreal tip_position, qreal base_position, qreal half_width) {
    if (qFuzzyIsNull(line.length())) {
        return {};
    }

    const qreal dx = line.dx() / line.length();
    const qreal dy = line.dy() / line.length();
    const QPointF normal(-dy, dx);
    const QPointF tip = line.pointAt(tip_position);
    const QPointF base = line.pointAt(base_position);
    return QPolygonF({
        tip,
        base + (normal * half_width),
        base - (normal * half_width),
    });
}

QString normalizedEdgeKey(const QString &from_cell, const QString &to_cell) {
    return from_cell < to_cell
        ? QString("%1|%2").arg(from_cell, to_cell)
        : QString("%1|%2").arg(to_cell, from_cell);
}

QString segmentTooltip(const RouteSegmentVisual &segment_visual) {
    if (!segment_visual.is_repeated) {
        return QString("航段: %1 → %2").arg(segment_visual.from_cell, segment_visual.to_cell);
    }

    const QString first_cell = segment_visual.from_cell < segment_visual.to_cell
        ? segment_visual.from_cell
        : segment_visual.to_cell;
    const QString second_cell = segment_visual.from_cell < segment_visual.to_cell
        ? segment_visual.to_cell
        : segment_visual.from_cell;
    return QString("重复航段: %1 ↔ %2 | 共 %3 次")
        .arg(first_cell, second_cell)
        .arg(segment_visual.repeat_count);
}
}

GridScene::GridScene(QObject *parent)
    : QGraphicsScene(parent) {
    initializeGrid();
}

void GridScene::setNoFlyCells(const QStringList &cells) {
    no_fly_cells_ = cells;
    resetCellBrushes();
}

void GridScene::setNoFlyEditEnabled(bool enabled) {
    no_fly_edit_enabled_ = enabled;
}

void GridScene::setCandidateNoFlyCells(const QStringList &cells) {
    candidate_no_fly_cells_ = cells;
    updateCandidateOverlays();
}

void GridScene::clearCandidateNoFlyCells() {
    candidate_no_fly_cells_.clear();
    updateCandidateOverlays();
}

QStringList GridScene::noFlyCells() const {
    return no_fly_cells_;
}

QStringList GridScene::candidateNoFlyCells() const {
    return candidate_no_fly_cells_;
}

void GridScene::setRoute(const QStringList &route) {
    clearRouteItems();

    QString error_message;
    const bool is_closed_route = !route.isEmpty() && route.first() == route.last();
    const bool route_is_valid = is_closed_route
        ? RouteValidator::validateClosedRoute(route, no_fly_cells_, route.first(), &error_message)
        : RouteValidator::validateRoute(route, no_fly_cells_, &error_message);
    if (!route_is_valid) {
        qWarning() << "Invalid route received:" << error_message;
    }

    const auto segment_visuals = RouteVisualizer::buildSegmentVisuals(route);

    QPen invalid_pen(QColor(220, 60, 60));
    invalid_pen.setWidthF(2.0);
    invalid_pen.setStyle(Qt::DashLine);
    invalid_pen.setCapStyle(Qt::RoundCap);
    invalid_pen.setJoinStyle(Qt::RoundJoin);

    QPen primary_halo_pen(QColor(255, 255, 255, 220));
    primary_halo_pen.setWidthF(5.0);
    primary_halo_pen.setCapStyle(Qt::RoundCap);
    primary_halo_pen.setJoinStyle(Qt::RoundJoin);
    primary_halo_pen.setCosmetic(true);

    QPen primary_route_pen(QColor(47, 111, 237));
    primary_route_pen.setWidthF(2.8);
    primary_route_pen.setCapStyle(Qt::RoundCap);
    primary_route_pen.setJoinStyle(Qt::RoundJoin);
    primary_route_pen.setCosmetic(true);

    QPen repeated_halo_pen(QColor(255, 255, 255, 235));
    repeated_halo_pen.setWidthF(11.0);
    repeated_halo_pen.setCapStyle(Qt::RoundCap);
    repeated_halo_pen.setJoinStyle(Qt::RoundJoin);
    repeated_halo_pen.setCosmetic(true);

    QPen repeated_route_pen(QColor(240, 140, 0));
    repeated_route_pen.setWidthF(7.0);
    repeated_route_pen.setCapStyle(Qt::RoundCap);
    repeated_route_pen.setJoinStyle(Qt::RoundJoin);
    repeated_route_pen.setCosmetic(true);

    QSet<QString> rendered_edges;
    QSet<QString> rendered_repeat_overlays;

    for (const auto &segment_visual : segment_visuals) {
        const bool adjacent = RouteValidator::isAdjacent(segment_visual.from_cell, segment_visual.to_cell);
        const QPointF from_center = cellCenter(segment_visual.from_cell);
        const QPointF to_center = cellCenter(segment_visual.to_cell);
        const QLineF center_line(from_center, to_center);
        const QString edge_key = normalizedEdgeKey(segment_visual.from_cell, segment_visual.to_cell);

        const QString tooltip = segmentTooltip(segment_visual);
        if (!adjacent) {
            auto *line = addLine(center_line, invalid_pen);
            line->setToolTip(tooltip);
            line->setZValue(3.0);
            route_items_.append(line);
            continue;
        }

        if (!rendered_edges.contains(edge_key)) {
            auto *underlay = addLine(center_line, primary_halo_pen);
            underlay->setToolTip(tooltip);
            underlay->setZValue(2.85);
            route_items_.append(underlay);

            auto *line = addLine(center_line, primary_route_pen);
            line->setToolTip(tooltip);
            line->setZValue(2.95);
            route_items_.append(line);
            rendered_edges.insert(edge_key);
        }

        if (segment_visual.is_repeated && !rendered_repeat_overlays.contains(edge_key)) {
            auto *repeat_halo = addLine(center_line, repeated_halo_pen);
            repeat_halo->setToolTip(tooltip);
            repeat_halo->setZValue(3.05);
            route_items_.append(repeat_halo);

            auto *repeat_line = addLine(center_line, repeated_route_pen);
            repeat_line->setToolTip(tooltip);
            repeat_line->setZValue(3.15);
            route_items_.append(repeat_line);

            const QPointF badge_center = center_line.pointAt(0.5);
            auto *badge = addEllipse(
                badge_center.x() - 12.0,
                badge_center.y() - 12.0,
                24.0,
                24.0,
                QPen(QColor(255, 255, 255, 240), 2.0),
                QBrush(QColor(240, 140, 0)));
            badge->setToolTip(tooltip);
            badge->setZValue(3.3);
            route_items_.append(badge);

            auto *badge_text = addSimpleText(segment_visual.badge_text);
            QFont badge_font = badge_text->font();
            badge_font.setBold(true);
            badge_font.setPointSizeF(9.5);
            badge_text->setFont(badge_font);
            badge_text->setBrush(Qt::white);
            const QRectF text_rect = badge_text->boundingRect();
            badge_text->setPos(
                badge_center.x() - (text_rect.width() / 2.0),
                badge_center.y() - (text_rect.height() / 2.0));
            badge_text->setToolTip(tooltip);
            badge_text->setZValue(3.35);
            route_items_.append(badge_text);

            rendered_repeat_overlays.insert(edge_key);
        }

        if (segment_visual.is_repeated) {
            QLineF arrow_line = center_line;
            arrow_line.translate(normalVector(center_line, arrowOffsetForPass(segment_visual.pass_index, segment_visual.repeat_count)));
            auto *arrow = addPolygon(
                buildArrowHead(arrow_line, 0.82, 0.66, 6.2),
                QPen(QColor(255, 255, 255, 220), 1.0),
                QBrush(QColor(240, 140, 0)));
            arrow->setToolTip(tooltip);
            arrow->setZValue(3.25);
            route_items_.append(arrow);
            continue;
        }

        auto *arrow = addPolygon(
            buildArrowHead(center_line, 0.80, 0.70, 4.8),
            Qt::NoPen,
            QBrush(QColor(47, 111, 237)));
        arrow->setToolTip(tooltip);
        arrow->setZValue(3.05);
        route_items_.append(arrow);
    }
}

void GridScene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsScene::mousePressEvent(event);

    if (!no_fly_edit_enabled_ || event->button() != Qt::LeftButton) {
        return;
    }

    for (auto *item : items(event->scenePos())) {
        const QVariant code_variant = item->data(0);
        if (!code_variant.canConvert<QString>()) {
            continue;
        }
        const QString code = code_variant.toString();
        if (code.isEmpty()) {
            continue;
        }
        emit cellClicked(code);
        break;
    }
}

void GridScene::setStartCell(const QString &cell_code) {
    if (start_marker_ != nullptr) {
        removeItem(start_marker_);
        delete start_marker_;
        start_marker_ = nullptr;
    }
    if (start_label_ != nullptr) {
        removeItem(start_label_);
        delete start_label_;
        start_label_ = nullptr;
    }

    if (cell_code.isEmpty()) {
        return;
    }

    const QPointF center = cellCenter(cell_code);
    QPen start_pen(QColor(47, 111, 237));
    start_pen.setWidthF(2.4);
    start_marker_ = addEllipse(
        center.x() - 13.0,
        center.y() - 13.0,
        26.0,
        26.0,
        start_pen,
        QBrush(QColor(47, 111, 237, 40)));
    start_marker_->setZValue(4.2);

    start_label_ = addSimpleText("起飞区");
    QFont label_font = start_label_->font();
    label_font.setBold(true);
    label_font.setPointSizeF(8.5);
    start_label_->setFont(label_font);
    start_label_->setBrush(QColor(30, 64, 175));
    start_label_->setPos(center.x() - 14.0, center.y() + 12.0);
    start_label_->setZValue(4.3);
}

void GridScene::setCurrentCell(const QString &cell_code) {
    if (current_marker_ != nullptr) {
        removeItem(current_marker_);
        delete current_marker_;
        current_marker_ = nullptr;
    }

    if (cell_code.isEmpty()) {
        return;
    }

    const QPointF center = cellCenter(cell_code);
    current_marker_ = addEllipse(
        center.x() - 9.0,
        center.y() - 9.0,
        18.0,
        18.0,
        QPen(QColor(255, 255, 255, 240), 2.0),
        QBrush(QColor(231, 76, 60)));
    current_marker_->setZValue(5.0);
}

void GridScene::setLandingTarget(
    const QString &terminal_cell,
    double takeoff_anchor_x_cm,
    double takeoff_anchor_y_cm,
    bool enabled) {
    if (terminal_marker_ != nullptr) {
        removeItem(terminal_marker_);
        delete terminal_marker_;
        terminal_marker_ = nullptr;
    }
    if (landing_corridor_ != nullptr) {
        removeItem(landing_corridor_);
        delete landing_corridor_;
        landing_corridor_ = nullptr;
    }
    if (terminal_label_ != nullptr) {
        removeItem(terminal_label_);
        delete terminal_label_;
        terminal_label_ = nullptr;
    }

    if (!enabled || terminal_cell.isEmpty()) {
        return;
    }

    const QPointF terminal_center = cellCenter(terminal_cell);
    terminal_marker_ = addEllipse(
        terminal_center.x() - 10.0,
        terminal_center.y() - 10.0,
        20.0,
        20.0,
        QPen(QColor(20, 140, 90), 2.0),
        Qt::NoBrush);
    terminal_marker_->setZValue(4.5);

    QPen corridor_pen(QColor(20, 140, 90));
    corridor_pen.setWidthF(2.0);
    corridor_pen.setStyle(Qt::DashLine);
    landing_corridor_ = addLine(QLineF(terminal_center, fieldPointToScene(takeoff_anchor_x_cm, takeoff_anchor_y_cm)), corridor_pen);
    landing_corridor_->setZValue(3.5);

    terminal_label_ = addSimpleText("降落终点");
    QFont label_font = terminal_label_->font();
    label_font.setBold(true);
    label_font.setPointSizeF(8.5);
    terminal_label_->setFont(label_font);
    terminal_label_->setBrush(QColor(20, 140, 90));
    terminal_label_->setPos(terminal_center.x() + 10.0, terminal_center.y() - 24.0);
    terminal_label_->setZValue(4.6);
}

QPointF GridScene::cellCenter(const QString &cell_code) const {
    const QPoint point = GridMapper::toPoint(cell_code);
    const qreal scene_y_index = (kRows - 1) - point.y();
    return QPointF((point.x() * kCellSize) + (kCellSize / 2.0),
                   (scene_y_index * kCellSize) + (kCellSize / 2.0));
}

QPointF GridScene::fieldPointToScene(double x_cm, double y_cm) const {
    return QPointF(
        ((x_cm - kFieldMarginCm) / kGridCellCm) * kCellSize,
        ((y_cm - kFieldMarginCm) / kGridCellCm) * kCellSize);
}

void GridScene::clearRouteItems() {
    for (auto *item : route_items_) {
        removeItem(item);
        delete item;
    }
    route_items_.clear();
}

void GridScene::initializeGrid() {
    setSceneRect(-0.5 * kCellSize, -0.5 * kCellSize, (kColumns + 1.0) * kCellSize, (kRows + 1.0) * kCellSize);
    setBackgroundBrush(QColor(240, 245, 251));

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const QString code = GridMapper::toCode(QPoint(x, (kRows - 1) - y));
            auto *rect = addRect(
                x * kCellSize,
                y * kCellSize,
                kCellSize,
                kCellSize,
                QPen(QColor(203, 213, 225), 1.0),
                QBrush(QColor(255, 255, 255)));
            rect->setData(0, code);
            rect->setZValue(1.0);
            cell_items_.insert(code, rect);

            auto *label = addSimpleText(code);
            QFont label_font = label->font();
            label_font.setPointSizeF(8.5);
            label_font.setBold(true);
            label->setFont(label_font);
            label->setBrush(QColor(100, 116, 139));
            label->setPos((x * kCellSize) + 4.0, (y * kCellSize) + 4.0);
            label->setZValue(2.0);
        }
    }
}

void GridScene::resetCellBrushes() {
    for (auto it = cell_items_.begin(); it != cell_items_.end(); ++it) {
        if (no_fly_cells_.contains(it.key())) {
            it.value()->setBrush(QBrush(QColor(203, 213, 225), Qt::Dense4Pattern));
        } else {
            it.value()->setBrush(QColor(255, 255, 255));
        }
    }
}

void GridScene::updateCandidateOverlays() {
    for (auto *overlay : candidate_cell_overlays_) {
        removeItem(overlay);
        delete overlay;
    }
    candidate_cell_overlays_.clear();

    if (candidate_no_fly_cells_.isEmpty()) {
        return;
    }

    QBrush overlay_brush(QColor(255, 170, 51, 180));
    overlay_brush.setStyle(Qt::SolidPattern);

    for (const QString &code : candidate_no_fly_cells_) {
        if (!cell_items_.contains(code)) {
            continue;
        }
        const auto *base_rect = cell_items_.value(code);
        auto *overlay = addRect(base_rect->rect(), Qt::NoPen, overlay_brush);
        overlay->setZValue(2.1);
        overlay->setData(0, code);
        candidate_cell_overlays_.insert(code, overlay);
    }
}
