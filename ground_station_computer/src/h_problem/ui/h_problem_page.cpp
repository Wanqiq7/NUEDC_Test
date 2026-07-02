#include "h_problem/ui/h_problem_page.h"

#include "competition_core/protocol/envelope_codec.h"
#include "h_problem/mission/h_protocol_adapter.h"
#include "h_problem/storage/h_mission_plan_store.h"
#include "h_problem/ui/h_grid_scene.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtGlobal>

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

constexpr int kMaxDetectionListItems = 500;

class AutoFitGraphicsView final : public QGraphicsView {
public:
    explicit AutoFitGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent) {
        setAlignment(Qt::AlignCenter);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fitSceneToViewport();
    }

    void showEvent(QShowEvent *event) override {
        QGraphicsView::showEvent(event);
        fitSceneToViewport();
    }

private:
    void fitSceneToViewport() {
        if (scene() == nullptr || scene()->sceneRect().isEmpty()) {
            return;
        }
        resetTransform();
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }
};

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
    detection_totals_ = repository_.summarizeByAnimal();
}

QWidget *HProblemTaskAdapter::createTaskView(QWidget *parent) {
    grid_scene_ = HProblemPage::createGridScene(parent);
    auto *view = new AutoFitGraphicsView(grid_scene_, parent);
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

    case_label_ = new QLabel("案例: 未加载", parent);
    case_label_->setObjectName("StatusText");
    mission_label_ = new QLabel("任务: 等待规划", parent);
    mission_label_->setObjectName("StatusText");

    auto *legend_label = new QLabel(parent);
    legend_label->setWordWrap(true);
    legend_label->setText(
        "<div style='line-height:1.7;'>"
        "<b style='color:#0f172a;'>路线图例</b><br/>"
        "<span style='color:#2f6fed;'>■</span> 主航线"
        "　<span style='color:#f08c00;'>■</span> 重复航段<br/>"
        "<span style='color:#14906f;'>■</span> 终点 / 45° 降落走廊"
        "<br/><span style='color:#475569;'>橙色圆牌显示重复次数，如 2x / 3x；箭头表示飞行方向。</span>"
        "</div>");

    detection_list_ = new QListWidget(parent);
    summary_table_ = new QTableWidget(0, 2, parent);
    summary_table_->setHorizontalHeaderLabels({"动物", "数量"});
    summary_table_->horizontalHeader()->setStretchLastSection(true);
    summary_table_->verticalHeader()->setVisible(false);
    summary_table_->setAlternatingRowColors(true);
    summary_table_->setSelectionMode(QAbstractItemView::NoSelection);
    updateSummaryTable(detection_totals_);

    auto *overview_card = new QFrame(parent);
    overview_card->setProperty("card", true);
    auto *overview_layout = new QVBoxLayout(overview_card);
    overview_layout->setContentsMargins(16, 16, 16, 16);
    overview_layout->setSpacing(8);
    auto *overview_title = new QLabel("任务概览", parent);
    overview_title->setObjectName("CardTitle");
    overview_layout->addWidget(overview_title);
    overview_layout->addWidget(case_label_);
    overview_layout->addWidget(mission_label_);
    overview_layout->addSpacing(4);
    overview_layout->addWidget(legend_label);

    auto *detection_card = new QFrame(parent);
    detection_card->setProperty("card", true);
    auto *detection_layout = new QVBoxLayout(detection_card);
    detection_layout->setContentsMargins(16, 16, 16, 16);
    detection_layout->setSpacing(10);
    auto *detection_title = new QLabel("实时检测记录", parent);
    detection_title->setObjectName("CardTitle");
    detection_layout->addWidget(detection_title);
    detection_layout->addWidget(detection_list_, 1);

    auto *summary_card = new QFrame(parent);
    summary_card->setProperty("card", true);
    auto *summary_layout = new QVBoxLayout(summary_card);
    summary_layout->setContentsMargins(16, 16, 16, 16);
    summary_layout->setSpacing(10);
    auto *summary_title = new QLabel("统计汇总", parent);
    summary_title->setObjectName("CardTitle");
    summary_layout->addWidget(summary_title);
    summary_layout->addWidget(summary_table_);

    auto *right_layout = new QVBoxLayout();
    right_layout->setSpacing(12);
    right_layout->addWidget(overview_card);
    right_layout->addWidget(detection_card, 1);
    right_layout->addWidget(summary_card);

    auto *root = new QWidget(parent);
    auto *root_layout = new QHBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(16);
    root_layout->addWidget(view, 3);
    root_layout->addLayout(right_layout, 2);
    return root;
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

MissionRuntimeInputs HProblemTaskAdapter::missionRuntimeInputs() const {
    return MissionRuntimeInputs{
        command_sync_enabled_,
        false,
        mission_synced_to_airborne_,
        mission_running_,
        current_case_id_,
        acknowledged_task_id_,
        acknowledged_mission_loaded_,
        last_accepted_sequence_,
    };
}

void HProblemTaskAdapter::setCommandSyncEnabled(bool enabled) {
    command_sync_enabled_ = enabled;
}

void HProblemTaskAdapter::setCommandClient(const ZmqCommandClient &client) {
    command_service_ = MissionCommandService(client);
}

void HProblemTaskAdapter::loadInitialPreview() {
    updateSummaryTable(detection_totals_);

    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, {});
    if (!result.ok) {
        qWarning() << "Failed to load initial mission preview:" << result.error_message;
        notifyStatusTextChanged(QString("状态: 默认航线加载失败 | %1").arg(result.error_message));
        return;
    }

    applyMissionPlanResult(result, false);
    notifyStatusTextChanged("状态: 默认航线已加载，可设置禁飞区");
}

void HProblemTaskAdapter::handleTaskPlan(const competition::TaskPlan &plan) {
    const TaskPlanMessage message = competition::taskPlanToMessage(plan);
    HGridConfigData config;
    QString error_message;
    if (!HProtocolAdapter::decodeGridConfig(message, &config, &error_message)) {
        notifyStatusTextChanged(QString("错误: %1").arg(error_message));
        return;
    }

    applyGridConfig(
        config.case_id,
        config.start_cell,
        config.no_fly_cells,
        config.route,
        config.terminal_cell,
        config.landing_enabled,
        config.descent_angle_deg,
        config.takeoff_anchor_x_cm,
        config.takeoff_anchor_y_cm);
}

void HProblemTaskAdapter::handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) {
    const TaskEventMessage message = competition::taskEventToMessage(event);
    QString error_message;
    if (event.event_type == "detection") {
        HDetectionData detection;
        if (!HProtocolAdapter::decodeDetection(message, &detection, &error_message)) {
            notifyStatusTextChanged(QString("错误: %1").arg(error_message));
            return;
        }
        applyDetection(detection.cell_code, detection.animal_name, detection.count, timestamp_ms);
        return;
    }

    HTelemetryData telemetry;
    if (!HProtocolAdapter::decodeTelemetry(message, &telemetry, &error_message)) {
        notifyStatusTextChanged(QString("错误: %1").arg(error_message));
        return;
    }
    applyTelemetry(telemetry.current_cell, telemetry.step_index, telemetry.visited_cells);
}

void HProblemTaskAdapter::handleTaskSummary(const competition::TaskSummary &summary) {
    const TaskSummaryMessage message = competition::taskSummaryToMessage(summary);
    HSummaryData data;
    QString error_message;
    if (!HProtocolAdapter::decodeSummary(message, &data, &error_message)) {
        notifyStatusTextChanged(QString("错误: %1").arg(error_message));
        return;
    }
    applySummary(data.totals, data.visited_cells);
}

void HProblemTaskAdapter::applyGridConfig(
    const QString &case_id,
    const QString &start_cell,
    const QStringList &no_fly_cells,
    const QStringList &route,
    const QString &terminal_cell,
    bool landing_enabled,
    double descent_angle_deg,
    double takeoff_anchor_x_cm,
    double takeoff_anchor_y_cm) {
    current_case_id_ = case_id;
    current_start_cell_ = start_cell;
    current_terminal_cell_ = terminal_cell;
    current_landing_enabled_ = landing_enabled;
    current_descent_angle_deg_ = descent_angle_deg;
    current_takeoff_anchor_x_cm_ = takeoff_anchor_x_cm;
    current_takeoff_anchor_y_cm_ = takeoff_anchor_y_cm;
    case_file_path_ = resolveCaseFilePath(case_id);
    committed_no_fly_cells_ = no_fly_cells;
    candidate_no_fly_cells_.clear();
    if (grid_scene_ != nullptr) {
        grid_scene_->clearCandidateNoFlyCells();
        grid_scene_->setNoFlyEditEnabled(false);
        grid_scene_->setNoFlyCells(no_fly_cells);
        grid_scene_->setStartCell(start_cell);
        grid_scene_->setRoute(route);
        grid_scene_->setLandingTarget(
            terminal_cell,
            takeoff_anchor_x_cm,
            takeoff_anchor_y_cm,
            landing_enabled);
        grid_scene_->setCurrentCell(start_cell);
    }
    planning_state_.handleGenerationSucceeded();
    mission_synced_to_airborne_ = true;
    refreshPlanningButtonText();
    refreshMissionContextLabels();
    emitRuntimeChanged();
    notifyStatusTextChanged("状态: 路线已加载，等待起飞");
}

void HProblemTaskAdapter::applyTelemetry(const QString &current_cell, int step_index, int visited_cells) {
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

void HProblemTaskAdapter::applyDetection(
    const QString &cell_code,
    const QString &animal_name,
    int count,
    qint64 timestamp_ms) {
    if (!repository_.storeDetection(cell_code, animal_name, count, timestamp_ms)) {
        qWarning() << "Failed to store detection" << cell_code << animal_name << count;
    }
    detection_totals_[animal_name] += count;
    if (detection_list_ != nullptr) {
        detection_list_->addItem(QString("[%1] %2 -> %3 x %4")
                                     .arg(QDateTime::fromMSecsSinceEpoch(timestamp_ms).toString("hh:mm:ss"))
                                     .arg(cell_code, animal_name)
                                     .arg(count));
        while (detection_list_->count() > kMaxDetectionListItems) {
            delete detection_list_->takeItem(0);
        }
    }
    updateSummaryTable(detection_totals_);
}

void HProblemTaskAdapter::applySummary(const QMap<QString, int> &totals, int visited_cells) {
    mission_running_ = false;
    emitRuntimeChanged();
    notifyStatusTextChanged(QString("状态: 巡查完成 | 已访问方格: %1").arg(visited_cells));
    detection_totals_ = totals;
    updateSummaryTable(detection_totals_);
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
    acknowledged_mission_loaded_ = true;
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

void HProblemTaskAdapter::markControlCommandStopped() {
    mission_running_ = false;
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

void HProblemTaskAdapter::markAirborneSyncState(bool online, bool synced) {
    if (!online) {
        clearCommandAckState();
    }
    mission_synced_to_airborne_ = synced;
    if (!synced) {
        mission_running_ = false;
        acknowledged_mission_loaded_ = false;
    }
    refreshMissionContextLabels();
    emitRuntimeChanged();
}

void HProblemTaskAdapter::applyCommandAck(const CommandSendResult &result) {
    if (!result.ok || (result.task_id.isEmpty() && result.last_accepted_sequence == 0)) {
        return;
    }

    if (!result.task_id.isEmpty()) {
        acknowledged_task_id_ = result.task_id;
    }
    acknowledged_mission_loaded_ = result.mission_loaded;
    mission_synced_to_airborne_ = result.mission_loaded;
    mission_running_ = result.mission_running;
    last_accepted_sequence_ = result.last_accepted_sequence;
    refreshMissionContextLabels();
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
    clearCommandAckState();
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
    clearCommandAckState();
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
        clearCommandAckState();
        emitRuntimeChanged();
        notifyStatusTextChanged("状态: 航线生成成功，可执行任务");
        return;
    }

    const auto send_result = command_service_.sendMissionPlan(result.plan);
    if (send_result.ok) {
        applyCommandAck(send_result);
        if (send_result.task_id.isEmpty() && send_result.last_accepted_sequence == 0) {
            mission_synced_to_airborne_ = true;
            mission_running_ = false;
            emitRuntimeChanged();
        }
        notifyStatusTextChanged("状态: 任务已同步至机载端，可执行任务");
        return;
    }

    mission_synced_to_airborne_ = false;
    mission_running_ = false;
    clearCommandAckState();
    emitRuntimeChanged();
    qWarning() << "Failed to sync mission plan to airborne NUC:" << send_result.message;
    notifyStatusTextChanged(QString("错误: 任务已本地生成，但机载端未确认 | %1").arg(send_result.message));
}

void HProblemTaskAdapter::refreshMissionContextLabels() {
    if (case_label_ == nullptr || mission_label_ == nullptr) {
        return;
    }

    const QString case_text = current_case_id_.isEmpty() ? QStringLiteral("未加载") : current_case_id_;
    const QString start_text = current_start_cell_.isEmpty() ? QStringLiteral("未知") : current_start_cell_;
    const QString terminal_text = current_terminal_cell_.isEmpty() ? QStringLiteral("未知") : current_terminal_cell_;
    case_label_->setText(QString("案例: %1 | 起点: %2 | 终点: %3").arg(case_text, start_text, terminal_text));

    if (current_case_id_.isEmpty()) {
        mission_label_->setText("任务: 等待规划");
        return;
    }

    QString mission_text;
    if (current_landing_enabled_) {
        mission_text = QString("任务: 最短飞行时间 | 斜降落角: %1°").arg(current_descent_angle_deg_, 0, 'f', 1);
    } else {
        mission_text = "任务: 标准巡查";
    }

    if (!acknowledged_task_id_.isEmpty() || last_accepted_sequence_ > 0) {
        const QString ack_task_text = acknowledged_task_id_.isEmpty() ? QStringLiteral("未知") : acknowledged_task_id_;
        const QString loaded_text = acknowledged_mission_loaded_ ? QStringLiteral("已加载") : QStringLiteral("未加载");
        const QString running_text = mission_running_ ? QStringLiteral("运行中") : QStringLiteral("待执行");
        mission_text += QString(" | 机载Ack: %1 / %2 / %3 / seq %4")
                            .arg(ack_task_text, loaded_text, running_text, QString::number(last_accepted_sequence_));
    }
    mission_label_->setText(mission_text);
}

void HProblemTaskAdapter::refreshPlanningButtonText() {
    notifyPlanningButtonTextChanged(planning_state_.primaryButtonText());
}

void HProblemTaskAdapter::emitRuntimeChanged() {
    notifyRuntimeChanged();
}

void HProblemTaskAdapter::clearCommandAckState() {
    acknowledged_task_id_.clear();
    acknowledged_mission_loaded_ = false;
    last_accepted_sequence_ = 0;
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

void HProblemTaskAdapter::updateSummaryTable(const QMap<QString, int> &totals) {
    if (summary_table_ == nullptr) {
        return;
    }

    summary_table_->setRowCount(totals.size());
    int row = 0;
    for (auto it = totals.begin(); it != totals.end(); ++it, ++row) {
        summary_table_->setItem(row, 0, new QTableWidgetItem(it.key()));
        summary_table_->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
    }
}

QVector<CompetitionTaskAdapterDescriptor> availableCompetitionTaskAdapters() {
    return {
        CompetitionTaskAdapterDescriptor{
            "h_problem",
            "H 题野生动物巡检",
            []() -> std::unique_ptr<CompetitionTaskAdapter> { return std::make_unique<HProblemTaskAdapter>(); },
        },
    };
}

QString configuredCompetitionTaskAdapterId() {
    const QString configured_id = qEnvironmentVariable("NUEDC_TASK_ADAPTER").trimmed();
    return configured_id.isEmpty() ? QStringLiteral("h_problem") : configured_id;
}

std::unique_ptr<CompetitionTaskAdapter> createCompetitionTaskAdapter(const QString &adapter_id, QString *error_message) {
    const QString selected_id = adapter_id.trimmed().isEmpty() ? QStringLiteral("h_problem") : adapter_id.trimmed();
    QStringList available_ids;
    for (const CompetitionTaskAdapterDescriptor &descriptor : availableCompetitionTaskAdapters()) {
        available_ids.append(descriptor.adapter_id);
        if (descriptor.adapter_id == selected_id) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return descriptor.create();
        }
    }

    if (error_message != nullptr) {
        *error_message = QString("unknown task adapter '%1'; available adapters: %2")
            .arg(selected_id, available_ids.join(", "));
    }
    return nullptr;
}

std::unique_ptr<CompetitionTaskAdapter> createConfiguredCompetitionTaskAdapter(QString *error_message) {
    return createCompetitionTaskAdapter(configuredCompetitionTaskAdapterId(), error_message);
}

std::unique_ptr<CompetitionTaskAdapter> createDefaultCompetitionTaskAdapter() {
    return createCompetitionTaskAdapter(QStringLiteral("h_problem"));
}



