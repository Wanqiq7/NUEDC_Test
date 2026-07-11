#pragma once

#include "h_problem/mission/h_mission_view_sink.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>

class GridScene;
class QLabel;
class QListWidget;
class QTableWidget;
class QWidget;

// H 题被动视图：只负责构建控件树并按语义化 setter 更新 UI，不含任何业务逻辑、
// 网络、存储或状态。所有「做什么」由 Adapter / Controller 决定，视图只负责「怎么显示」。
// 控件仍由 Qt 父对象树持有（沿用原 Adapter 的所有权模型），本类只存裸指针。
// 实现 HMissionViewSink，使控制器可仅依赖窄接口而不依赖具体 UI 类型。
class HProblemView : public HMissionViewSink {
public:
    using CellClickedHandler = std::function<void(const QString &)>;

    // 构建并返回根控件；initial_totals 用于首帧填充统计表。
    QWidget *buildWidget(QWidget *parent, const QMap<QString, int> &initial_totals);

    // 网格被点击时的回调（选择禁飞格）。在 buildWidget 之后设置即可。
    void setCellClickedHandler(CellClickedHandler handler) { cell_clicked_handler_ = std::move(handler); }

    // 概览标签。
    void setCaseLabel(const QString &text) override;
    void setMissionLabel(const QString &text) override;
    void setTargetStatus(const QString &text) override;

    // 展示已规划航线（禁飞格 / 航线 / 起点 / 终点降落走廊），并关闭禁飞编辑。
    void showRoute(
        const QStringList &no_fly_cells,
        const QStringList &route,
        const QString &start_cell,
        const QString &terminal_cell,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm,
        bool landing_enabled) override;

    // 进入禁飞格编辑模式：清候选、清禁飞、开启编辑。
    void enterNoFlyEditMode() override;

    // 更新候选禁飞格高亮。
    void setCandidateCells(const QStringList &cells) override;

    // 更新当前无人机所在格。
    void setCurrentCell(const QString &cell_code) override;

    // 追加一条实时检测记录（含超限裁剪）。
    void appendDetection(const QString &text) override;

    // 重建统计汇总表。
    void setSummaryTotals(const QMap<QString, int> &totals) override;

private:
    GridScene *grid_scene_ = nullptr;
    QLabel *case_label_ = nullptr;
    QLabel *mission_label_ = nullptr;
    QLabel *target_status_label_ = nullptr;
    QListWidget *detection_list_ = nullptr;
    QTableWidget *summary_table_ = nullptr;
    CellClickedHandler cell_clicked_handler_;
};
