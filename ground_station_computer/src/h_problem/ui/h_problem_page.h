#pragma once

#include "framework/task/competition_task_adapter.h"
#include "h_problem/mission/h_mission_command_service.h"
#include "h_problem/mission/h_no_fly_planning_state.h"
#include "h_problem/mission/h_route_planner_bridge.h"
#include "h_problem/rules/h_no_fly_zone_rules.h"
#include "h_problem/storage/h_detection_repository.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

class GridScene;
class QLabel;
class QListWidget;
class QObject;
class QTableWidget;
class ZmqCommandClient;

class HProblemPage {
public:
    static GridScene *createGridScene(QObject *parent);
};

class HProblemTaskAdapter : public CompetitionTaskAdapter {
public:
    HProblemTaskAdapter();

    QWidget *createTaskView(QWidget *parent) override;
    QString initialPlanningButtonText() const override;
    QString activeTaskId() const override;
    bool missionSyncedToAirborne() const override;
    bool missionRunning() const override;
    MissionRuntimeInputs missionRuntimeInputs() const override;

    void setCommandSyncEnabled(bool enabled) override;
    void setCommandClient(const ZmqCommandClient &client) override;
    void loadInitialPreview() override;
    void handleTaskPlan(const competition::TaskPlan &plan) override;
    void handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) override;
    void handleTaskSummary(const competition::TaskSummary &summary) override;
    void handlePlanningButtonClicked() override;
    void markControlCommandStarted() override;
    void markControlCommandStopped() override;
    void markAirborneSyncState(bool online, bool synced) override;
    void applyCommandAck(const CommandSendResult &result) override;

private:
    void applyGridConfig(
        const QString &case_id,
        const QString &start_cell,
        const QStringList &no_fly_cells,
        const QStringList &route,
        const QString &terminal_cell,
        bool landing_enabled,
        double descent_angle_deg,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm);
    void applyTelemetry(const QString &current_cell, int step_index, int visited_cells);
    void applyDetection(const QString &cell_code, const QString &animal_name, int count, qint64 timestamp_ms);
    void applySummary(const QMap<QString, int> &totals, int visited_cells);
    void handleGridSceneCellClicked(const QString &cell_code);
    void enterNoFlySelectionMode();
    void generateMissionPlanFromCandidateSelection();
    void applyMissionPlanResult(const MissionPlanResult &result, bool sync_to_airborne = false);
    void refreshMissionContextLabels();
    void refreshPlanningButtonText();
    void emitRuntimeChanged();
    void clearCommandAckState();
    QString resolveCaseFilePath(const QString &case_id) const;
    void updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation);
    void updateSummaryTable(const QMap<QString, int> &totals);

    GridScene *grid_scene_ = nullptr;
    QLabel *case_label_ = nullptr;
    QLabel *mission_label_ = nullptr;
    QListWidget *detection_list_ = nullptr;
    QTableWidget *summary_table_ = nullptr;
    PlanningStateMachine planning_state_;
    MissionPlanBridge mission_plan_bridge_;
    MissionCommandService command_service_;
    QString case_file_path_ = "shared/cases/sample_case.json";
    QString mission_plan_output_path_ = "runtime/active_mission_plan.json";
    QString current_case_id_;
    QString current_start_cell_;
    QString current_terminal_cell_;
    QStringList candidate_no_fly_cells_;
    QStringList committed_no_fly_cells_;
    bool current_landing_enabled_ = false;
    double current_descent_angle_deg_ = 0.0;
    double current_takeoff_anchor_x_cm_ = 0.0;
    double current_takeoff_anchor_y_cm_ = 0.0;
    bool command_sync_enabled_ = true;
    bool mission_synced_to_airborne_ = false;
    bool mission_running_ = false;
    QString acknowledged_task_id_;
    bool acknowledged_mission_loaded_ = false;
    quint64 last_accepted_sequence_ = 0;
    DetectionRepository repository_;
    QMap<QString, int> detection_totals_;
};
