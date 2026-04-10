#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QStringList>

#include "detection_repository.h"
#include "mission_plan_bridge.h"
#include "no_fly_zone_rules.h"
#include "planning_state_machine.h"

class GridScene;
class QLabel;
class QListWidget;
class QTableWidget;
class ZmqSubscriberWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool start_worker = true);
    ~MainWindow() override;

private slots:
    void handleGridConfig(
        QString case_id,
        QString start_cell,
        QStringList no_fly_cells,
        QStringList route,
        QString terminal_cell,
        bool landing_enabled,
        double descent_angle_deg,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm);
    void handleTelemetry(QString current_cell, int step_index, int visited_cells);
    void handleDetection(QString cell_code, QString animal_name, int count, qint64 timestamp_ms);
    void handleSummary(QMap<QString, int> totals, int visited_cells);
    void handleError(QString message);

private:
    void handlePlanningButtonClicked();
    void handleGridSceneCellClicked(const QString &cell_code);

    void loadInitialMissionPreview();
    void enterNoFlySelectionMode();
    void generateMissionPlanFromCandidateSelection();
    void applyMissionPlanResult(const MissionPlanResult &result);
    bool persistMissionPlan(const MissionPlanData &plan, QString *error_message = nullptr) const;
    void refreshMissionContextLabels();
    void refreshPlanningButtonText();
    QString resolveCaseFilePath(const QString &case_id) const;
    void updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation);

    void refreshSummaryFromDatabase();
    void updateSummaryTable(const QMap<QString, int> &totals);

    GridScene *grid_scene_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *case_label_ = nullptr;
    QLabel *mission_label_ = nullptr;
    QLabel *legend_label_ = nullptr;
    QListWidget *detection_list_ = nullptr;
    QTableWidget *summary_table_ = nullptr;

    QPushButton *planning_button_ = nullptr;
    PlanningStateMachine planning_state_;
    MissionPlanBridge mission_plan_bridge_;
    QString case_file_path_ = "cases/sample_case.json";
    QString mission_plan_output_path_ = "cases/active_mission_plan.json";
    QString current_case_id_;
    QString current_start_cell_;
    QString current_terminal_cell_;
    QStringList candidate_no_fly_cells_;
    QStringList committed_no_fly_cells_;
    bool current_landing_enabled_ = false;
    double current_descent_angle_deg_ = 0.0;
    double current_takeoff_anchor_x_cm_ = 0.0;
    double current_takeoff_anchor_y_cm_ = 0.0;

    DetectionRepository repository_;
    ZmqSubscriberWorker *worker_ = nullptr;
};
