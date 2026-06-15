#include "h_problem/ui/h_problem_page.h"

#include "framework/storage/mission_plan_store.h"
#include "h_problem/ui/h_grid_scene.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsView>
#include <QPainter>

namespace {
QString findRepositoryRootImpl() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("shared/cases/sample_case.json"))
            && dir.exists(QStringLiteral("ground_station_computer/src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::current().absolutePath();
}

const QString &repositoryRootPath() {
    static const QString root = findRepositoryRootImpl();
    return root;
}

}

GridScene *HProblemPage::createGridScene(QObject *parent) {
    auto *scene = new GridScene(parent);
    scene->setObjectName("GridScene");
    return scene;
}

HProblemTaskAdapter::HProblemTaskAdapter()
    : repository_(QDir(repositoryRootPath()).filePath("runtime/ground_control_results.db")) {
    const QString repo_root = repositoryRootPath();
    case_file_path_ = QDir(repo_root).filePath(case_file_path_);
    mission_plan_output_path_ = QDir(repo_root).filePath(mission_plan_output_path_);
    repository_.open();
    notifySummaryTotalsChanged(repository_.summarizeByAnimal());
}

QWidget *HProblemTaskAdapter::createTaskView(QWidget *parent) {
    grid_scene_ = HProblemPage::createGridScene(parent);
    auto *view = new QGraphicsView(grid_scene_, parent);
    view->setObjectName("TaskView");
    view->setRenderHints(QPainter::Antialiasing);
    view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet(
        "QGraphicsView {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "    stop:0 #f8fbff, stop:1 #edf3fb);"
        "  border: 1px solid #d8e1ee;"
        "  border-radius: 18px;"
        "}"
    );
    QObject::connect(grid_scene_, &GridScene::cellClicked, view, [this](const QString &cell_code) {
        handleGridSceneCellClicked(cell_code);
    });
    return view;
}

QString HProblemTaskAdapter::initialPlanningButtonText() const {
    return planning_state_.primaryButtonText();
}

QString HProblemTaskAdapter::activeTaskId() const {
    return current_case_id_;
}

bool HProblemTaskAdapter::missionSyncedToAirborne() const {
    return mission_synced_to_airborne_;
}

bool HProblemTaskAdapter::missionRunning() const {
    return mission_running_;
}

void HProblemTaskAdapter::setCommandSyncEnabled(bool enabled) {
    command_sync_enabled_ = enabled;
}

void HProblemTaskAdapter::setCommandClient(const ZmqCommandClient &client) {
    command_service_ = MissionCommandService(client);
}

void HProblemTaskAdapter::loadInitialPreview() {
    notifySummaryTotalsChanged(repository_.summarizeByAnimal());

    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, {});
    if (!result.ok) {
        qWarning() << "Failed to load initial mission preview:" << result.error_message;
        notifyStatusTextChanged(QString("状态: 默认航线加载失败 | %1").arg(result.error_message));
        return;
    }

    applyMissionPlanResult(result, false);
    notifyStatusTextChanged("状态: 默认航线已加载，可设置禁飞区");
}

void HProblemTaskAdapter::handleGridConfig(const TaskGridConfig &config) {
    current_case_id_ = config.case_id;
    current_start_cell_ = config.start_cell;
    current_terminal_cell_ = config.terminal_cell;
    current_landing_enabled_ = config.landing_enabled;
    current_descent_angle_deg_ = config.descent_angle_deg;
    current_takeoff_anchor_x_cm_ = config.takeoff_anchor_x_cm;
    current_takeoff_anchor_y_cm_ = config.takeoff_anchor_y_cm;
    case_file_path_ = resolveCaseFilePath(config.case_id);
    committed_no_fly_cells_ = config.no_fly_cells;
    candidate_no_fly_cells_.clear();
    if (grid_scene_ != nullptr) {
        grid_scene_->clearCandidateNoFlyCells();
        grid_scene_->setNoFlyEditEnabled(false);
        grid_scene_->setNoFlyCells(config.no_fly_cells);
        grid_scene_->setStartCell(config.start_cell);
        grid_scene_->setRoute(config.route);
        grid_scene_->setLandingTarget(
            config.terminal_cell,
            config.takeoff_anchor_x_cm,
            config.takeoff_anchor_y_cm,
            config.landing_enabled);
        grid_scene_->setCurrentCell(config.start_cell);
    }
    planning_state_.handleGenerationSucceeded();
    mission_synced_to_airborne_ = true;
    refreshPlanningButtonText();
    refreshMissionContextLabels();
    emitRuntimeChanged();
    notifyStatusTextChanged("状态: 路线已加载，等待起飞");
}

void HProblemTaskAdapter::handleTelemetry(const QString &current_cell, int step_index, int visited_cells) {
    mission_running_ = true;
    emitRuntimeChanged();
    notifyStatusTextChanged(QString("状态: 巡查中 | 当前方格: %1 | 步数: %2 | 已访问: %3")
                               .arg(current_cell)
                               .arg(step_index)
                               .arg(visited_cells));
    if (grid_scene_ != nullptr) {
        grid_scene_->setCurrentCell(current_cell);
    }
}

void HProblemTaskAdapter::handleDetection(
    const QString &cell_code,
    const QString &animal_name,
    int count,
    qint64 timestamp_ms) {
    repository_.storeDetection(cell_code, animal_name, count, timestamp_ms);
    notifyDetectionRecordAdded(QString("[%1] %2 -> %3 x %4")
                                  .arg(QDateTime::fromMSecsSinceEpoch(timestamp_ms).toString("hh:mm:ss"))
                                  .arg(cell_code, animal_name)
                                  .arg(count));
    notifySummaryTotalsChanged(repository_.summarizeByAnimal());
}

void HProblemTaskAdapter::handleSummary(const QMap<QString, int> &totals, int visited_cells) {
    mission_running_ = false;
    emitRuntimeChanged();
    notifyStatusTextChanged(QString("状态: 巡查完成 | 已访问方格: %1").arg(visited_cells));
    notifySummaryTotalsChanged(totals);
}

void HProblemTaskAdapter::handlePlanningButtonClicked() {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        enterNoFlySelectionMode();
        return;
    }

    if (planning_state_.state() == PlanningUiState::SelectingNoFlyZone) {
        notifyStatusTextChanged("状态: 请继续选择 3 个横向或纵向连续的禁飞格");
        return;
    }

    if (planning_state_.state() == PlanningUiState::ReadyToGenerate) {
        generateMissionPlanFromCandidateSelection();
    }
}

void HProblemTaskAdapter::markControlCommandStarted() {
    mission_running_ = true;
    emitRuntimeChanged();
}

void HProblemTaskAdapter::markControlCommandStopped() {
    mission_running_ = false;
    emitRuntimeChanged();
}

void HProblemTaskAdapter::markAirborneSyncState(bool online, bool synced) {
    Q_UNUSED(online);
    mission_synced_to_airborne_ = synced;
    if (!synced) {
        mission_running_ = false;
    }
    emitRuntimeChanged();
}

void HProblemTaskAdapter::handleGridSceneCellClicked(const QString &cell_code) {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        return;
    }

    if (candidate_no_fly_cells_.contains(cell_code)) {
        candidate_no_fly_cells_.removeAll(cell_code);
    } else if (candidate_no_fly_cells_.size() < 3) {
        candidate_no_fly_cells_.append(cell_code);
    } else {
        notifyStatusTextChanged("状态: 已选择 3 个禁飞格，请先取消一个");
        return;
    }

    if (grid_scene_ != nullptr) {
        grid_scene_->setCandidateNoFlyCells(candidate_no_fly_cells_);
    }
    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    planning_state_.updateSelectionValidity(validation.is_valid);
    refreshPlanningButtonText();
    updateStatusForCandidateSelection(validation);
}

void HProblemTaskAdapter::enterNoFlySelectionMode() {
    planning_state_.handlePrimaryButtonClicked();
    candidate_no_fly_cells_.clear();
    mission_synced_to_airborne_ = false;
    mission_running_ = false;
    if (grid_scene_ != nullptr) {
        grid_scene_->clearCandidateNoFlyCells();
        grid_scene_->setNoFlyCells({});
        grid_scene_->setNoFlyEditEnabled(true);
    }
    refreshPlanningButtonText();
    emitRuntimeChanged();
    notifyStatusTextChanged("状态: 请选择 3 个横向或纵向连续的禁飞格");
}

void HProblemTaskAdapter::generateMissionPlanFromCandidateSelection() {
    if (current_start_cell_.isEmpty()) {
        notifyStatusTextChanged("错误: 当前起飞格未知，无法生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() != 3) {
        notifyStatusTextChanged("状态: 请选择 3 个禁飞格后再生成");
        return;
    }

    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    if (!validation.is_valid) {
        updateStatusForCandidateSelection(validation);
        return;
    }

    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, candidate_no_fly_cells_);
    if (!result.ok) {
        notifyStatusTextChanged(QString("错误: %1").arg(result.error_message));
        return;
    }

    applyMissionPlanResult(result, true);
}

void HProblemTaskAdapter::applyMissionPlanResult(const MissionPlanResult &result, bool sync_to_airborne) {
    case_file_path_ = resolveCaseFilePath(result.plan.case_id);
    current_case_id_ = result.plan.case_id;
    current_start_cell_ = result.plan.start_cell;
    current_terminal_cell_ = result.plan.terminal_cell;
    committed_no_fly_cells_ = result.plan.no_fly_cells;
    current_landing_enabled_ = result.plan.landing_enabled;
    current_descent_angle_deg_ = result.plan.descent_angle_deg.value_or(0.0);
    current_takeoff_anchor_x_cm_ = result.plan.takeoff_anchor_x_cm.value_or(0.0);
    current_takeoff_anchor_y_cm_ = result.plan.takeoff_anchor_y_cm.value_or(0.0);

    candidate_no_fly_cells_.clear();
    if (grid_scene_ != nullptr) {
        grid_scene_->clearCandidateNoFlyCells();
        grid_scene_->setNoFlyEditEnabled(false);
        grid_scene_->setNoFlyCells(committed_no_fly_cells_);
        grid_scene_->setRoute(result.plan.route);
        grid_scene_->setStartCell(current_start_cell_);
        grid_scene_->setLandingTarget(
            current_terminal_cell_,
            current_takeoff_anchor_x_cm_,
            current_takeoff_anchor_y_cm_,
            current_landing_enabled_);
        grid_scene_->setCurrentCell(current_start_cell_);
    }

    planning_state_.handleGenerationSucceeded();
    refreshPlanningButtonText();
    refreshMissionContextLabels();

    QString persist_error;
    if (!MissionPlanStore(mission_plan_output_path_).save(result.plan, &persist_error)) {
        qWarning() << "Failed to persist mission plan:" << persist_error;
        notifyStatusTextChanged(QString("错误: 本地航线已更新，但同步模拟器任务计划失败 | %1").arg(persist_error));
        return;
    }

    if (!sync_to_airborne || !command_sync_enabled_) {
        mission_synced_to_airborne_ = false;
        mission_running_ = false;
        emitRuntimeChanged();
        notifyStatusTextChanged("状态: 航线生成成功，可执行任务");
        return;
    }

    const auto send_result = command_service_.sendMissionPlan(result.plan);
    if (send_result.ok) {
        mission_synced_to_airborne_ = true;
        mission_running_ = false;
        emitRuntimeChanged();
        notifyStatusTextChanged("状态: 任务已同步至机载端，可执行任务");
        return;
    }

    mission_synced_to_airborne_ = false;
    mission_running_ = false;
    emitRuntimeChanged();
    qWarning() << "Failed to sync mission plan to airborne NUC:" << send_result.message;
    notifyStatusTextChanged(QString("错误: 任务已本地生成，但机载端未确认 | %1").arg(send_result.message));
}

void HProblemTaskAdapter::refreshMissionContextLabels() {
    const QString case_text = current_case_id_.isEmpty() ? QStringLiteral("未加载") : current_case_id_;
    const QString start_text = current_start_cell_.isEmpty() ? QStringLiteral("未知") : current_start_cell_;
    const QString terminal_text = current_terminal_cell_.isEmpty() ? QStringLiteral("未知") : current_terminal_cell_;
    notifyCaseTextChanged(QString("案例: %1 | 起点: %2 | 终点: %3").arg(case_text, start_text, terminal_text));

    if (current_case_id_.isEmpty()) {
        notifyMissionTextChanged("任务: 等待规划");
        return;
    }

    if (current_landing_enabled_) {
        notifyMissionTextChanged(QString("任务: 最短飞行时间 | 斜降落角: %1°").arg(current_descent_angle_deg_, 0, 'f', 1));
    } else {
        notifyMissionTextChanged("任务: 标准巡查");
    }
}

void HProblemTaskAdapter::refreshPlanningButtonText() {
    notifyPlanningButtonTextChanged(planning_state_.primaryButtonText());
}

void HProblemTaskAdapter::emitRuntimeChanged() {
    notifyRuntimeChanged(mission_synced_to_airborne_, mission_running_);
}

QString HProblemTaskAdapter::resolveCaseFilePath(const QString &case_id) const {
    if (case_id.isEmpty()) {
        return case_file_path_;
    }

    const QString candidate_path = QDir(repositoryRootPath()).filePath(QStringLiteral("shared/cases/%1.json").arg(case_id));
    if (QFileInfo::exists(candidate_path)) {
        return candidate_path;
    }
    return case_file_path_;
}

void HProblemTaskAdapter::updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation) {
    if (validation.is_valid) {
        notifyStatusTextChanged("状态: 已选择 3/3 个禁飞格，可生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() < 3) {
        notifyStatusTextChanged(QString("状态: 已选择 %1/3 个禁飞格").arg(candidate_no_fly_cells_.size()));
        return;
    }

    notifyStatusTextChanged(QString("状态: %1").arg(validation.message));
}

CompetitionTaskAdapter *createDefaultCompetitionTaskAdapter() {
    return new HProblemTaskAdapter();
}
