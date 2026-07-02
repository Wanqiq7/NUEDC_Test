#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// 控制器（HMissionController）驱动视图所依赖的窄接口。视图实现它，控制器只依赖它，
// 从而控制器不反向依赖任何具体 UI 类型，可用 mock sink 单测工作流逻辑。
// 方法集与 HProblemView 的语义化 setter 一一对应。
class HMissionViewSink {
public:
    virtual ~HMissionViewSink() = default;

    virtual void setCaseLabel(const QString &text) = 0;
    virtual void setMissionLabel(const QString &text) = 0;
    virtual void showRoute(
        const QStringList &no_fly_cells,
        const QStringList &route,
        const QString &start_cell,
        const QString &terminal_cell,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm,
        bool landing_enabled) = 0;
    virtual void enterNoFlyEditMode() = 0;
    virtual void setCandidateCells(const QStringList &cells) = 0;
    virtual void setCurrentCell(const QString &cell_code) = 0;
    virtual void appendDetection(const QString &text) = 0;
    virtual void setSummaryTotals(const QMap<QString, int> &totals) = 0;
};
