#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QMap>
#include <QStringList>

class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QGraphicsLineItem;
class QGraphicsSceneMouseEvent;

class GridScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit GridScene(QObject *parent = nullptr);

    void setNoFlyCells(const QStringList &cells);
    void setRoute(const QStringList &route);
    void setStartCell(const QString &cell_code);
    void setCurrentCell(const QString &cell_code);
    void setLandingTarget(
        const QString &descent_start_cell,
        double touchdown_x_cm,
        double touchdown_y_cm,
        bool enabled);
    void setNoFlyEditEnabled(bool enabled);
    void setCandidateNoFlyCells(const QStringList &cells);
    void clearCandidateNoFlyCells();
    QStringList noFlyCells() const;
    QStringList candidateNoFlyCells() const;

signals:
    void cellClicked(QString cell_code);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QPointF cellCenter(const QString &cell_code) const;
    QPointF fieldPointToScene(double x_cm, double y_cm) const;
    void clearRouteItems();
    void initializeGrid();
    void resetCellBrushes();
    void updateCandidateOverlays();

    QMap<QString, QGraphicsRectItem *> cell_items_;
    QList<QGraphicsItem *> route_items_;
    QGraphicsEllipseItem *start_marker_ = nullptr;
    QGraphicsSimpleTextItem *start_label_ = nullptr;
    QGraphicsEllipseItem *current_marker_ = nullptr;
    QGraphicsEllipseItem *descent_start_marker_ = nullptr;
    QGraphicsSimpleTextItem *descent_start_label_ = nullptr;
    QGraphicsEllipseItem *touchdown_marker_ = nullptr;
    QGraphicsSimpleTextItem *touchdown_label_ = nullptr;
    QGraphicsLineItem *landing_corridor_ = nullptr;
    QStringList no_fly_cells_;
    bool no_fly_edit_enabled_ = false;
    QStringList candidate_no_fly_cells_;
    QMap<QString, QGraphicsRectItem *> candidate_cell_overlays_;
};
