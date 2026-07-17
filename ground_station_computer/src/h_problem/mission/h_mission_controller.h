#pragma once

#include "framework/runtime/airborne_sync_state.h"
#include "framework/runtime/mission_runtime_state.h"
#include "competition_core/task/models.h"
#include "h_problem/mission/h_mission_command_service.h"
#include "h_problem/mission/h_mission_view_sink.h"
#include "h_problem/mission/h_no_fly_planning_state.h"
#include "h_problem/mission/h_route_planner_bridge.h"
#include "h_problem/rules/h_no_fly_zone_rules.h"
#include "h_problem/storage/h_detection_repository.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>

class ZmqCommandClient;

// H 题任务工作流控制器：持有规划状态机、航线桥、命令服务、机载同步态、检测存储与
// 全部业务字段，处理协议事件解码、规划流程与持久化。通过窄接口 HMissionViewSink
// 驱动视图，通过注入的回调向外通知状态文本 / 规划按钮文本 / 运行态变化。
// 不持有任何具体 UI 类型，可用 mock sink 单测。逻辑与原 HProblemTaskAdapter 1:1 迁移。
class HMissionController {
public:
    using TextCallback = std::function<void(const QString &)>;
    using RuntimeCallback = std::function<void()>;
    using CommandLinkStateCallback = std::function<void(const CommandSendResult &)>;

    HMissionController(
        HMissionViewSink *sink,
        TextCallback status_text_callback,
        TextCallback planning_button_text_callback,
        RuntimeCallback runtime_callback,
        CommandLinkStateCallback command_link_state_callback = {},
        QString detection_database_path = {});

    // 查询。
    QString initialPlanningButtonText() const;
    QString activeTaskId() const;
    bool missionSyncedToAirborne() const;
    bool missionRunning() const;
    MissionRuntimeInputs missionRuntimeInputs() const;
    const QMap<QString, int> &detectionTotals() const { return detection_totals_; }
    QStringList detectedAnimalNames() const;
    QMap<QString, int> detectionLocations(const QString &animal_name) const;

    // 配置。
    void setCommandSyncEnabled(bool enabled);
    void setCommandClient(const ZmqCommandClient &client);
    void setCommandTransport(const CommandTransport *transport);

    // 生命周期 / 事件。
    void loadInitialPreview();
    void handleTaskPlan(const competition::TaskPlan &plan);
    void handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms);
    void handleTaskSummary(const competition::TaskSummary &summary);
    void handlePlanningButtonClicked();
    void handleGridSceneCellClicked(const QString &cell_code);
    void markControlCommandStarted();
    void markControlCommandStopped();
    void markAirborneSyncState(bool online, bool synced);
    void applyCommandAck(const CommandSendResult &result);

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
        double takeoff_anchor_y_cm,
        double touchdown_x_cm,
        double touchdown_y_cm,
        double estimated_mission_time_s,
        const QString &planning_optimality,
        const QStringList &planning_warnings);
    void applyTelemetry(const QString &current_cell, int step_index, int visited_cells);
    void applyDetection(
        const QString &task_id,
        const QString &track_id,
        const QString &cell_code,
        const QString &animal_name,
        int count,
        qint64 timestamp_ms);
    void applyTargetUpdate(
        const QString &track_id,
        const QString &cell_code,
        const QString &animal_name,
        double score,
        int target_offset_x_px,
        int target_offset_y_px);
    void applySummary(const QMap<QString, int> &totals, int visited_cells);
    CommandSendResult disarmVisionTargetingForLifecycle();
    void enterNoFlySelectionMode();
    void generateTaskPlanFromCandidateSelection();
    void applyTaskPlan(const competition::TaskPlan &plan, bool sync_to_airborne = false);
    void refreshMissionContextLabels();
    void refreshPlanningButtonText();
    void emitRuntimeChanged();
    QString resolveCaseFilePath(const QString &case_id) const;
    void updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation);

    void notifyStatusText(const QString &text) const;
    void notifyPlanningButtonText(const QString &text) const;
    void notifyRuntimeChanged() const;
    void notifyCommandLinkResult(const CommandSendResult &result) const;
    bool isCurrentTaskMessage(const QString &task_id) const;

    HMissionViewSink *sink_ = nullptr;
    TextCallback status_text_callback_;
    TextCallback planning_button_text_callback_;
    RuntimeCallback runtime_callback_;
    CommandLinkStateCallback command_link_state_callback_;

    PlanningStateMachine planning_state_;
    HRoutePlanner route_planner_;
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
    double current_touchdown_x_cm_ = 0.0;
    double current_touchdown_y_cm_ = 0.0;
    double current_estimated_mission_time_s_ = 0.0;
    QString current_planning_optimality_;
    QStringList current_planning_warnings_;
    bool command_sync_enabled_ = true;
    AirborneSyncState sync_state_;
    DetectionRepository repository_;
    QMap<QString, int> detection_totals_;
};
