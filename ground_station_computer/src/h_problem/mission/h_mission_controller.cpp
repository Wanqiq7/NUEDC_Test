#include "h_problem/mission/h_mission_controller.h"

#include "competition_core/mission/task_plan_store.h"
#include "competition_core/protocol/envelope_codec.h"
#include "framework/config/repository_paths.h"
#include "h_problem/mission/h_protocol_adapter.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

HMissionController::HMissionController(
    HMissionViewSink *sink,
    TextCallback status_text_callback,
    TextCallback planning_button_text_callback,
    RuntimeCallback runtime_callback,
    CommandLinkStateCallback command_link_state_callback)
    : sink_(sink),
      status_text_callback_(std::move(status_text_callback)),
      planning_button_text_callback_(std::move(planning_button_text_callback)),
      runtime_callback_(std::move(runtime_callback)),
      command_link_state_callback_(std::move(command_link_state_callback)),
      repository_(RepositoryPaths::resolve(QStringLiteral("runtime/ground_control_results.db"))) {
    case_file_path_ = RepositoryPaths::resolve(case_file_path_);
    mission_plan_output_path_ = RepositoryPaths::resolve(mission_plan_output_path_);
    repository_.open();
    detection_totals_ = repository_.summarizeByAnimal();
}

void HMissionController::notifyStatusText(const QString &text) const {
    if (status_text_callback_) {
        status_text_callback_(text);
    }
}

void HMissionController::notifyPlanningButtonText(const QString &text) const {
    if (planning_button_text_callback_) {
        planning_button_text_callback_(text);
    }
}

void HMissionController::notifyRuntimeChanged() const {
    if (runtime_callback_) {
        runtime_callback_();
    }
}

void HMissionController::notifyCommandLinkResult(const CommandSendResult &result) const {
    if (command_link_state_callback_) {
        command_link_state_callback_(result);
    }
}

QString HMissionController::initialPlanningButtonText() const {
    return planning_state_.primaryButtonText();
}

QString HMissionController::activeTaskId() const {
    return current_case_id_;
}

bool HMissionController::missionSyncedToAirborne() const {
    return sync_state_.syncedToAirborne();
}

bool HMissionController::missionRunning() const {
    return sync_state_.running();
}

MissionRuntimeInputs HMissionController::missionRuntimeInputs() const {
    MissionRuntimeInputs inputs;
    inputs.command_sync_enabled = command_sync_enabled_;
    inputs.airborne_online = false;
    inputs.active_task_id = current_case_id_;
    sync_state_.fillRuntimeInputs(inputs);
    return inputs;
}

void HMissionController::setCommandSyncEnabled(bool enabled) {
    command_sync_enabled_ = enabled;
}

void HMissionController::setCommandClient(const ZmqCommandClient &client) {
    command_service_ = MissionCommandService(client);
}

void HMissionController::setCommandTransport(const CommandTransport *transport) {
    command_service_.setCommandTransport(transport);
}

void HMissionController::loadInitialPreview() {
    sink_->setSummaryTotals(detection_totals_);

    const auto result = route_planner_.generatePlan(case_file_path_, {});
    if (!result.ok) {
        qWarning() << "Failed to load initial mission preview:" << result.failure_reason;
        notifyStatusText(QString("状态: 默认航线加载失败 | %1").arg(result.failure_reason));
        return;
    }

    applyTaskPlan(result.plan, false);
    notifyStatusText("状态: 默认航线已加载，可设置禁飞区");
}

void HMissionController::handleTaskPlan(const competition::TaskPlan &plan) {
    HGridConfigData config;
    QString error_message;
    if (!HProtocolAdapter::decodeTaskPlan(plan, &config, &error_message)) {
        notifyStatusText(QString("错误: %1").arg(error_message));
        return;
    }

    const CommandSendResult disarm_result = disarmVisionTargetingForLifecycle();
    sync_state_.clearAck();

    applyGridConfig(
        config.case_id,
        config.start_cell,
        config.no_fly_cells,
        config.route,
        config.terminal_cell,
        config.landing_enabled,
        config.descent_angle_deg,
        config.takeoff_anchor_x_cm,
        config.takeoff_anchor_y_cm,
        config.touchdown_x_cm,
        config.touchdown_y_cm,
        config.estimated_mission_time_s,
        config.planning_optimality,
        config.planning_warnings);
    if (!disarm_result.ok) {
        notifyStatusText(QString("警告: 路线已替换，但视觉解除失败 | %1").arg(disarm_result.message));
    }
    notifyCommandLinkResult(disarm_result);
}

void HMissionController::handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) {
    if (!isCurrentTaskMessage(event.task_id)) {
        qDebug() << "Ignoring H problem event for another task" << event.task_id
                 << "current task" << current_case_id_;
        return;
    }

    const TaskEventMessage message = competition::taskEventToMessage(event);
    QString error_message;
    if (event.event_type == "detection") {
        HDetectionData detection;
        if (!HProtocolAdapter::decodeDetection(message, &detection, &error_message)) {
            notifyStatusText(QString("错误: %1").arg(error_message));
            return;
        }
        applyDetection(
            event.task_id,
            detection.track_id,
            detection.cell_code,
            detection.animal_name,
            detection.count,
            timestamp_ms);
        return;
    }

    if (event.event_type == "target_update") {
        HTargetUpdateData target_update;
        if (!HProtocolAdapter::decodeTargetUpdate(message, &target_update, &error_message)) {
            notifyStatusText(QString("错误: %1").arg(error_message));
            return;
        }
        applyTargetUpdate(
            target_update.track_id,
            target_update.cell_code,
            target_update.animal_name,
            target_update.score,
            target_update.target_offset_x_px,
            target_update.target_offset_y_px);
        return;
    }

    if (event.event_type == "telemetry") {
        HTelemetryData telemetry;
        if (!HProtocolAdapter::decodeTelemetry(message, &telemetry, &error_message)) {
            notifyStatusText(QString("错误: %1").arg(error_message));
            return;
        }
        applyTelemetry(telemetry.current_cell, telemetry.step_index, telemetry.visited_cells);
        return;
    }

    qWarning() << "Ignoring unknown H problem event type" << event.event_type;
}

void HMissionController::handleTaskSummary(const competition::TaskSummary &summary) {
    if (!isCurrentTaskMessage(summary.task_id) || summary.task_type != "h_problem") {
        qDebug() << "Ignoring H problem summary for another task or task type"
                 << summary.task_id << summary.task_type << "current task" << current_case_id_;
        return;
    }

    if (!summary.success) {
        QString failure_reason = QStringLiteral("机载执行器报告任务失败");
        const QJsonDocument payload = QJsonDocument::fromJson(summary.payload_json.toUtf8());
        if (payload.isObject()) {
            const QString reported_error = payload.object().value("error").toString().trimmed();
            if (!reported_error.isEmpty()) {
                failure_reason = reported_error;
            }
        }
        sync_state_.setRunning(false);
        const CommandSendResult disarm_result = disarmVisionTargetingForLifecycle();
        emitRuntimeChanged();
        QString status = QString("错误: 巡查失败 | %1").arg(failure_reason);
        if (!disarm_result.ok) {
            status += QString(" | 警告: 视觉解除失败: %1").arg(disarm_result.message);
        }
        notifyStatusText(status);
        notifyCommandLinkResult(disarm_result);
        return;
    }

    const TaskSummaryMessage message = competition::taskSummaryToMessage(summary);
    HSummaryData data;
    QString error_message;
    if (!HProtocolAdapter::decodeSummary(message, &data, &error_message)) {
        notifyStatusText(QString("错误: %1").arg(error_message));
        return;
    }
    applySummary(data.totals, data.visited_cells);
}

void HMissionController::applyGridConfig(
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
    const QStringList &planning_warnings) {
    current_case_id_ = case_id;
    current_start_cell_ = start_cell;
    current_terminal_cell_ = terminal_cell;
    current_landing_enabled_ = landing_enabled;
    current_descent_angle_deg_ = descent_angle_deg;
    current_takeoff_anchor_x_cm_ = takeoff_anchor_x_cm;
    current_takeoff_anchor_y_cm_ = takeoff_anchor_y_cm;
    current_touchdown_x_cm_ = touchdown_x_cm;
    current_touchdown_y_cm_ = touchdown_y_cm;
    current_estimated_mission_time_s_ = estimated_mission_time_s;
    current_planning_optimality_ = planning_optimality;
    current_planning_warnings_ = planning_warnings;
    case_file_path_ = resolveCaseFilePath(case_id);
    committed_no_fly_cells_ = no_fly_cells;
    candidate_no_fly_cells_.clear();
    sink_->showRoute(
        no_fly_cells,
        route,
        start_cell,
        terminal_cell,
        touchdown_x_cm != 0.0 ? touchdown_x_cm : takeoff_anchor_x_cm,
        touchdown_y_cm != 0.0 ? touchdown_y_cm : takeoff_anchor_y_cm,
        landing_enabled);
    planning_state_.handleGenerationSucceeded();
    sync_state_.setSyncedToAirborne(true);
    refreshPlanningButtonText();
    refreshMissionContextLabels();
    emitRuntimeChanged();
    notifyStatusText("状态: 路线已加载，等待起飞");
}

void HMissionController::applyTelemetry(const QString &current_cell, int step_index, int visited_cells) {
    notifyStatusText(QString("状态: 巡查中 | 当前方格: %1 | 步数: %2 | 已访问: %3")
                         .arg(current_cell)
                         .arg(step_index)
                         .arg(visited_cells));
    sink_->setCurrentCell(current_cell);
}

void HMissionController::applyDetection(
    const QString &task_id,
    const QString &track_id,
    const QString &cell_code,
    const QString &animal_name,
    int count,
    qint64 timestamp_ms) {
    const DetectionRepository::StoreResult store_result = repository_.storeDetection(
        task_id,
        track_id,
        cell_code,
        animal_name,
        count,
        timestamp_ms);
    if (store_result == DetectionRepository::StoreResult::Duplicate) {
        qDebug() << "Ignoring duplicate detection" << task_id << track_id;
        return;
    }
    if (store_result != DetectionRepository::StoreResult::Stored) {
        qWarning() << "Failed to store detection" << cell_code << animal_name << count;
        return;
    }
    detection_totals_[animal_name] += count;
    sink_->appendDetection(QString("[%1] %2 -> %3 x %4")
                               .arg(QDateTime::fromMSecsSinceEpoch(timestamp_ms).toString("hh:mm:ss"))
                               .arg(cell_code, animal_name)
                               .arg(count));
    sink_->setSummaryTotals(detection_totals_);
}

void HMissionController::applyTargetUpdate(
    const QString &track_id,
    const QString &cell_code,
    const QString &animal_name,
    double score,
    int target_offset_x_px,
    int target_offset_y_px) {
    sink_->setTargetStatus(
        QString("目标: %1 | %2 | %3 | 置信度: %4 | 偏移: (%5, %6) px")
            .arg(track_id, cell_code, animal_name)
            .arg(score, 0, 'f', 2)
            .arg(target_offset_x_px)
            .arg(target_offset_y_px));
    sink_->setCurrentCell(cell_code);
}

void HMissionController::applySummary(const QMap<QString, int> &totals, int visited_cells) {
    sync_state_.setRunning(false);
    const CommandSendResult disarm_result = disarmVisionTargetingForLifecycle();
    emitRuntimeChanged();
    QString status = QString("状态: 巡查完成 | 已访问方格: %1").arg(visited_cells);
    if (!disarm_result.ok) {
        status += QString(" | 警告: 视觉解除失败: %1").arg(disarm_result.message);
    }
    detection_totals_ = totals;
    sink_->setSummaryTotals(detection_totals_);
    notifyStatusText(status);
    notifyCommandLinkResult(disarm_result);
}

void HMissionController::handlePlanningButtonClicked() {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        enterNoFlySelectionMode();
        return;
    }

    if (planning_state_.state() == PlanningUiState::SelectingNoFlyZone) {
        notifyStatusText("状态: 请继续选择 3 个横向或纵向连续的禁飞格");
        return;
    }

    if (planning_state_.state() == PlanningUiState::ReadyToGenerate) {
        generateTaskPlanFromCandidateSelection();
    }
}

void HMissionController::markControlCommandStarted() {
    sync_state_.markControlStarted();
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

void HMissionController::markControlCommandStopped() {
    sync_state_.markControlStopped();
    const CommandSendResult disarm_result = disarmVisionTargetingForLifecycle();
    if (!disarm_result.ok) {
        notifyStatusText(QString("警告: 停止任务已确认，但视觉解除失败 | %1").arg(disarm_result.message));
    }
    notifyCommandLinkResult(disarm_result);
}

void HMissionController::markAirborneSyncState(bool online, bool synced) {
    sync_state_.applyAirborneSync(online, synced);
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

void HMissionController::applyCommandAck(const CommandSendResult &result) {
    if (result.task_id.isEmpty()) {
        qDebug() << "Ignoring command ACK without task identity";
        return;
    }

    if (!result.task_id.isEmpty() && !isCurrentTaskMessage(result.task_id)) {
        qDebug() << "Ignoring command ACK for another task" << result.task_id
                 << "current task" << current_case_id_;
        return;
    }

    if (!sync_state_.applyCommandAck(result)) {
        return;
    }
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

bool HMissionController::isCurrentTaskMessage(const QString &task_id) const {
    return !task_id.isEmpty() && task_id == current_case_id_;
}

void HMissionController::handleGridSceneCellClicked(const QString &cell_code) {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        return;
    }

    if (candidate_no_fly_cells_.contains(cell_code)) {
        candidate_no_fly_cells_.removeAll(cell_code);
    } else if (candidate_no_fly_cells_.size() < 3) {
        candidate_no_fly_cells_.append(cell_code);
    } else {
        notifyStatusText("状态: 已选择 3 个禁飞格，请先取消一个");
        return;
    }

    sink_->setCandidateCells(candidate_no_fly_cells_);
    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    planning_state_.updateSelectionValidity(validation.is_valid);
    refreshPlanningButtonText();
    updateStatusForCandidateSelection(validation);
}

void HMissionController::enterNoFlySelectionMode() {
    planning_state_.handlePrimaryButtonClicked();
    candidate_no_fly_cells_.clear();
    sync_state_.reset();
    sink_->enterNoFlyEditMode();
    refreshPlanningButtonText();
    emitRuntimeChanged();
    notifyStatusText("状态: 请选择 3 个横向或纵向连续的禁飞格");
}

void HMissionController::generateTaskPlanFromCandidateSelection() {
    if (current_start_cell_.isEmpty()) {
        notifyStatusText("错误: 当前起飞格未知，无法生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() != 3) {
        notifyStatusText("状态: 请选择 3 个禁飞格后再生成");
        return;
    }

    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    if (!validation.is_valid) {
        updateStatusForCandidateSelection(validation);
        return;
    }

    const auto result = route_planner_.generatePlan(case_file_path_, candidate_no_fly_cells_);
    if (!result.ok) {
        notifyStatusText(QString("错误: %1").arg(result.failure_reason));
        return;
    }

    applyTaskPlan(result.plan, true);
}

void HMissionController::applyTaskPlan(const competition::TaskPlan &plan, bool sync_to_airborne) {
    HGridConfigData config;
    QString decode_error;
    if (!HProtocolAdapter::decodeTaskPlan(plan, &config, &decode_error)) {
        notifyStatusText(QString("错误: %1").arg(decode_error));
        return;
    }
    const CommandSendResult disarm_result = disarmVisionTargetingForLifecycle();
    case_file_path_ = resolveCaseFilePath(config.case_id);
    current_case_id_ = config.case_id;
    current_start_cell_ = config.start_cell;
    current_terminal_cell_ = config.terminal_cell;
    committed_no_fly_cells_ = config.no_fly_cells;
    sync_state_.clearAck();
    current_landing_enabled_ = config.landing_enabled;
    current_descent_angle_deg_ = config.descent_angle_deg;
    current_takeoff_anchor_x_cm_ = config.takeoff_anchor_x_cm;
    current_takeoff_anchor_y_cm_ = config.takeoff_anchor_y_cm;
    current_touchdown_x_cm_ = config.touchdown_x_cm;
    current_touchdown_y_cm_ = config.touchdown_y_cm;
    current_estimated_mission_time_s_ = config.estimated_mission_time_s;
    current_planning_optimality_ = config.planning_optimality;
    current_planning_warnings_ = config.planning_warnings;

    candidate_no_fly_cells_.clear();
    sink_->showRoute(
        committed_no_fly_cells_,
        config.route,
        current_start_cell_,
        current_terminal_cell_,
        current_touchdown_x_cm_ != 0.0 ? current_touchdown_x_cm_ : current_takeoff_anchor_x_cm_,
        current_touchdown_y_cm_ != 0.0 ? current_touchdown_y_cm_ : current_takeoff_anchor_y_cm_,
        current_landing_enabled_);

    planning_state_.handleGenerationSucceeded();
    refreshPlanningButtonText();
    refreshMissionContextLabels();

    QString persist_error;
    if (!competition::storeTaskPlan(plan, mission_plan_output_path_, &persist_error)) {
        qWarning() << "Failed to persist mission plan:" << persist_error;
        QString status = QString("错误: 本地航线已更新，但同步模拟器任务计划失败 | %1").arg(persist_error);
        if (!disarm_result.ok) {
            status += QString(" | 视觉解除失败: %1").arg(disarm_result.message);
        }
        notifyStatusText(status);
        notifyCommandLinkResult(disarm_result);
        return;
    }

    if (!sync_to_airborne || !command_sync_enabled_) {
        sync_state_.reset();
        emitRuntimeChanged();
        QString status = QStringLiteral("状态: 航线生成成功，可执行任务");
        if (!disarm_result.ok) {
            status += QString(" | 警告: 视觉解除失败: %1").arg(disarm_result.message);
        }
        notifyStatusText(status);
        notifyCommandLinkResult(disarm_result);
        return;
    }

    const auto send_result = command_service_.sendTaskPlan(plan);
    if (send_result.ok
        && send_result.task_id == plan.task_id
        && send_result.mission_loaded) {
        applyCommandAck(send_result);
        QString status = QStringLiteral("状态: 任务已同步至机载端，可执行任务");
        if (!disarm_result.ok) {
            status += QString(" | 警告: 视觉解除失败: %1").arg(disarm_result.message);
        }
        notifyStatusText(status);
        notifyCommandLinkResult(disarm_result);
        notifyCommandLinkResult(send_result);
        return;
    }

    sync_state_.reset();
    emitRuntimeChanged();
    const QString failure_message = send_result.ok
        ? QStringLiteral("机载 ACK 缺少匹配的任务身份或加载状态")
        : send_result.message;
    qWarning() << "Failed to sync mission plan to airborne NUC:" << failure_message;
    QString status = QString("错误: 任务已本地生成，但机载端未确认 | %1").arg(failure_message);
    if (!disarm_result.ok) {
        status += QString(" | 视觉解除失败: %1").arg(disarm_result.message);
    }
    notifyStatusText(status);
    notifyCommandLinkResult(disarm_result);
    notifyCommandLinkResult(send_result);
}

CommandSendResult HMissionController::disarmVisionTargetingForLifecycle() {
    if (sink_ != nullptr) {
        sink_->setTargetStatus(QStringLiteral("目标: 等待跟踪"));
    }
    sync_state_.disarmVisionTargeting();

    if (!command_sync_enabled_ || current_case_id_.isEmpty()) {
        refreshMissionContextLabels();
        emitRuntimeChanged();
        return CommandSendResult{true, "vision targeting cleared locally"};
    }

    const CommandSendResult result = command_service_.disarmVisionTargeting(current_case_id_);
    if (result.ok) {
        sync_state_.applyCommandAck(result);
    } else {
        qWarning() << "Failed to disarm vision targeting during mission lifecycle:" << result.message;
    }
    refreshMissionContextLabels();
    emitRuntimeChanged();
    return result;
}

void HMissionController::refreshMissionContextLabels() {
    const QString case_text = current_case_id_.isEmpty() ? QStringLiteral("未加载") : current_case_id_;
    const QString start_text = current_start_cell_.isEmpty() ? QStringLiteral("未知") : current_start_cell_;
    const QString terminal_text = current_terminal_cell_.isEmpty() ? QStringLiteral("未知") : current_terminal_cell_;
    sink_->setCaseLabel(QString("案例: %1 | 起点: %2 | 终点: %3").arg(case_text, start_text, terminal_text));

    if (current_case_id_.isEmpty()) {
        sink_->setMissionLabel("任务: 等待规划");
        return;
    }

    QString mission_text;
    if (current_landing_enabled_) {
        mission_text = QString("任务: 最短飞行时间 | 斜降落角: %1°").arg(current_descent_angle_deg_, 0, 'f', 1);
    } else {
        mission_text = "任务: 标准巡查";
    }
    if (current_estimated_mission_time_s_ > 0.0) {
        mission_text += QString(" | 预计: %1s").arg(current_estimated_mission_time_s_, 0, 'f', 1);
    }
    if (!current_planning_optimality_.isEmpty()) {
        mission_text += QString(" | 规划: %1").arg(current_planning_optimality_);
    }
    if (!current_planning_warnings_.isEmpty()) {
        mission_text += QString(" | 警告: %1").arg(current_planning_warnings_.join("; "));
    }

    if (sync_state_.hasAck()) {
        const QString ack_task_text = sync_state_.acknowledgedTaskId().isEmpty()
                                          ? QStringLiteral("未知")
                                          : sync_state_.acknowledgedTaskId();
        const QString loaded_text = sync_state_.acknowledgedMissionLoaded() ? QStringLiteral("已加载") : QStringLiteral("未加载");
        const QString running_text = sync_state_.running() ? QStringLiteral("运行中") : QStringLiteral("待执行");
        const QString vision_text = sync_state_.visionArmed() ? QStringLiteral("已武装") : QStringLiteral("未武装");
        mission_text += QString(" | 机载Ack: %1 / %2 / %3 / 视觉瞄准: %4 / seq %5")
                            .arg(ack_task_text, loaded_text, running_text, vision_text,
                                 QString::number(sync_state_.lastAcceptedSequence()));
    } else if (sync_state_.visionArmed()) {
        mission_text += QStringLiteral(" | 视觉瞄准: 已武装");
    }
    sink_->setMissionLabel(mission_text);
}

void HMissionController::refreshPlanningButtonText() {
    notifyPlanningButtonText(planning_state_.primaryButtonText());
}

void HMissionController::emitRuntimeChanged() {
    notifyRuntimeChanged();
}

QString HMissionController::resolveCaseFilePath(const QString &case_id) const {
    if (case_id.isEmpty()) {
        return case_file_path_;
    }

    const QString candidate_path = RepositoryPaths::resolve(QStringLiteral("shared/cases/%1.json").arg(case_id));
    if (QFileInfo::exists(candidate_path)) {
        return candidate_path;
    }
    return case_file_path_;
}

void HMissionController::updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation) {
    if (validation.is_valid) {
        notifyStatusText("状态: 已选择 3/3 个禁飞格，可生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() < 3) {
        notifyStatusText(QString("状态: 已选择 %1/3 个禁飞格").arg(candidate_no_fly_cells_.size()));
        return;
    }

    notifyStatusText(QString("状态: %1").arg(validation.message));
}
