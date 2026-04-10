#include "main_window.h"

#include "grid_scene.h"
#include "zmq_subscriber_worker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsView>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace {
QString findRepositoryRootImpl() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("cases")) && dir.exists(QStringLiteral("python"))) {
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

MainWindow::MainWindow(QWidget *parent, bool start_worker)
    : QMainWindow(parent),
      repository_(QDir(repositoryRootPath()).filePath("ground_station_results.db")) {
    const QString repo_root = repositoryRootPath();
    case_file_path_ = QDir(repo_root).filePath(case_file_path_);
    mission_plan_output_path_ = QDir(repo_root).filePath(mission_plan_output_path_);

    auto *central = new QWidget(this);
    auto *root_layout = new QHBoxLayout(central);
    auto *left_layout = new QVBoxLayout();
    auto *right_layout = new QVBoxLayout();
    root_layout->setContentsMargins(18, 18, 18, 18);
    root_layout->setSpacing(16);
    left_layout->setSpacing(10);
    right_layout->setSpacing(12);

    central->setStyleSheet(
        "QWidget { background: #f4f7fb; color: #1f2937; }"
        "QFrame[card='true'] {"
        "  background: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid #d8e1ee;"
        "  border-radius: 16px;"
        "}"
        "QLabel#PanelTitle { color: #0f172a; font-size: 18px; font-weight: 700; }"
        "QLabel#PanelSubtitle { color: #475569; font-size: 13px; }"
        "QLabel#CardTitle { color: #0f172a; font-size: 15px; font-weight: 700; }"
        "QLabel#StatusText { color: #334155; font-size: 13px; padding: 2px 0; }"
        "QListWidget, QTableWidget {"
        "  background: #fcfdff;"
        "  border: 1px solid #d8e1ee;"
        "  border-radius: 12px;"
        "  padding: 6px;"
        "  gridline-color: #e2e8f0;"
        "}"
        "QHeaderView::section {"
        "  background: #eef3fb;"
        "  color: #334155;"
        "  border: none;"
        "  border-bottom: 1px solid #d8e1ee;"
        "  padding: 8px 6px;"
        "  font-weight: 700;"
        "}"
    );

    grid_scene_ = new GridScene(this);
    grid_scene_->setObjectName("GridScene");
    auto *view = new QGraphicsView(grid_scene_, this);
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

    auto *preview_title = new QLabel("航线预览", this);
    preview_title->setObjectName("PanelTitle");
    auto *preview_subtitle = new QLabel("起飞前先展示整条路线，蓝线表示主航线，橙色高亮带强调重复航段。", this);
    preview_subtitle->setObjectName("PanelSubtitle");
    preview_subtitle->setWordWrap(true);
    left_layout->addWidget(preview_title);
    left_layout->addWidget(preview_subtitle);
    left_layout->addWidget(view, 1);

    planning_button_ = new QPushButton(planning_state_.primaryButtonText(), this);
    planning_button_->setObjectName("PlanningButton");
    planning_button_->setFixedHeight(46);
    planning_button_->setCursor(Qt::PointingHandCursor);
    left_layout->addWidget(planning_button_);

    case_label_ = new QLabel("案例: 未加载", this);
    case_label_->setObjectName("StatusText");
    status_label_ = new QLabel("状态: 等待消息", this);
    status_label_->setObjectName("StatusText");
    mission_label_ = new QLabel("任务: 等待规划", this);
    mission_label_->setObjectName("StatusText");
    legend_label_ = new QLabel(this);
    legend_label_->setWordWrap(true);
    legend_label_->setText(
        "<div style='line-height:1.7;'>"
        "<b style='color:#0f172a;'>路线图例</b><br/>"
        "<span style='color:#2f6fed;'>■</span> 主航线"
        "　<span style='color:#f08c00;'>■</span> 重复航段<br/>"
        "<span style='color:#14906f;'>■</span> 终点 / 45° 降落走廊"
        "<br/><span style='color:#475569;'>橙色圆牌显示重复次数，如 2x / 3x；箭头表示飞行方向。</span>"
        "</div>");
    detection_list_ = new QListWidget(this);
    summary_table_ = new QTableWidget(0, 2, this);
    summary_table_->setHorizontalHeaderLabels({"动物", "数量"});
    summary_table_->horizontalHeader()->setStretchLastSection(true);
    summary_table_->verticalHeader()->setVisible(false);
    summary_table_->setAlternatingRowColors(true);
    summary_table_->setSelectionMode(QAbstractItemView::NoSelection);

    auto *overview_card = new QFrame(this);
    overview_card->setProperty("card", true);
    auto *overview_layout = new QVBoxLayout(overview_card);
    overview_layout->setContentsMargins(16, 16, 16, 16);
    overview_layout->setSpacing(8);
    auto *overview_title = new QLabel("任务概览", this);
    overview_title->setObjectName("CardTitle");
    overview_layout->addWidget(overview_title);
    overview_layout->addWidget(case_label_);
    overview_layout->addWidget(status_label_);
    overview_layout->addWidget(mission_label_);
    overview_layout->addSpacing(4);
    overview_layout->addWidget(legend_label_);

    auto *detection_card = new QFrame(this);
    detection_card->setProperty("card", true);
    auto *detection_layout = new QVBoxLayout(detection_card);
    detection_layout->setContentsMargins(16, 16, 16, 16);
    detection_layout->setSpacing(10);
    auto *detection_title = new QLabel("实时检测记录", this);
    detection_title->setObjectName("CardTitle");
    detection_layout->addWidget(detection_title);
    detection_layout->addWidget(detection_list_, 1);

    auto *summary_card = new QFrame(this);
    summary_card->setProperty("card", true);
    auto *summary_layout = new QVBoxLayout(summary_card);
    summary_layout->setContentsMargins(16, 16, 16, 16);
    summary_layout->setSpacing(10);
    auto *summary_title = new QLabel("统计汇总", this);
    summary_title->setObjectName("CardTitle");
    summary_layout->addWidget(summary_title);
    summary_layout->addWidget(summary_table_);

    right_layout->addWidget(overview_card);
    right_layout->addWidget(detection_card, 1);
    right_layout->addWidget(summary_card);

    root_layout->addLayout(left_layout, 3);
    root_layout->addLayout(right_layout, 2);
    setCentralWidget(central);
    resize(1100, 620);
    setWindowTitle("H 题混合联调地面站");

    repository_.open();
    refreshSummaryFromDatabase();

    connect(planning_button_, &QPushButton::clicked, this, &MainWindow::handlePlanningButtonClicked);
    connect(grid_scene_, &GridScene::cellClicked, this, &MainWindow::handleGridSceneCellClicked);
    loadInitialMissionPreview();

    worker_ = new ZmqSubscriberWorker("tcp://127.0.0.1:5557", this);
    if (start_worker) {
        connect(worker_, &ZmqSubscriberWorker::gridConfigReceived, this, &MainWindow::handleGridConfig);
        connect(worker_, &ZmqSubscriberWorker::telemetryReceived, this, &MainWindow::handleTelemetry);
        connect(worker_, &ZmqSubscriberWorker::detectionReceived, this, &MainWindow::handleDetection);
        connect(worker_, &ZmqSubscriberWorker::summaryReceived, this, &MainWindow::handleSummary);
        connect(worker_, &ZmqSubscriberWorker::errorOccurred, this, &MainWindow::handleError);
        worker_->start();
    }
}

MainWindow::~MainWindow() {
    if (worker_ != nullptr) {
        worker_->requestInterruption();
        worker_->wait(1500);
    }
}

void MainWindow::handleGridConfig(
    QString case_id,
    QString start_cell,
    QStringList no_fly_cells,
    QStringList route,
    QString terminal_cell,
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
    grid_scene_->clearCandidateNoFlyCells();
    grid_scene_->setNoFlyEditEnabled(false);
    planning_state_.handleGenerationSucceeded();
    refreshPlanningButtonText();
    refreshMissionContextLabels();
    status_label_->setText("状态: 路线已加载，等待起飞");
    grid_scene_->setNoFlyCells(no_fly_cells);
    grid_scene_->setStartCell(start_cell);
    grid_scene_->setRoute(route);
    grid_scene_->setLandingTarget(terminal_cell, takeoff_anchor_x_cm, takeoff_anchor_y_cm, landing_enabled);
    grid_scene_->setCurrentCell(start_cell);
}

void MainWindow::handleTelemetry(QString current_cell, int step_index, int visited_cells) {
    status_label_->setText(QString("状态: 巡查中 | 当前方格: %1 | 步数: %2 | 已访问: %3")
                               .arg(current_cell)
                               .arg(step_index)
                               .arg(visited_cells));
    grid_scene_->setCurrentCell(current_cell);
}

void MainWindow::handleDetection(QString cell_code, QString animal_name, int count, qint64 timestamp_ms) {
    repository_.storeDetection(cell_code, animal_name, count, timestamp_ms);
    detection_list_->addItem(QString("[%1] %2 -> %3 x %4")
                                 .arg(QDateTime::fromMSecsSinceEpoch(timestamp_ms).toString("hh:mm:ss"))
                                 .arg(cell_code, animal_name)
                                 .arg(count));
    refreshSummaryFromDatabase();
}

void MainWindow::handleSummary(QMap<QString, int> totals, int visited_cells) {
    status_label_->setText(QString("状态: 巡查完成 | 已访问方格: %1").arg(visited_cells));
    updateSummaryTable(totals);
}

void MainWindow::handleError(QString message) {
    status_label_->setText(QString("错误: %1").arg(message));
}

void MainWindow::refreshSummaryFromDatabase() {
    updateSummaryTable(repository_.summarizeByAnimal());
}

void MainWindow::updateSummaryTable(const QMap<QString, int> &totals) {
    summary_table_->setRowCount(totals.size());
    int row = 0;
    for (auto it = totals.begin(); it != totals.end(); ++it, ++row) {
        summary_table_->setItem(row, 0, new QTableWidgetItem(it.key()));
        summary_table_->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
    }
}

void MainWindow::handlePlanningButtonClicked() {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        enterNoFlySelectionMode();
        return;
    }

    if (planning_state_.state() == PlanningUiState::SelectingNoFlyZone) {
        status_label_->setText("状态: 请继续选择 3 个横向或纵向连续的禁飞格");
        return;
    }

    if (planning_state_.state() == PlanningUiState::ReadyToGenerate) {
        generateMissionPlanFromCandidateSelection();
    }
}

void MainWindow::handleGridSceneCellClicked(const QString &cell_code) {
    if (planning_state_.state() == PlanningUiState::IdlePreview) {
        return;
    }

    if (candidate_no_fly_cells_.contains(cell_code)) {
        candidate_no_fly_cells_.removeAll(cell_code);
    } else if (candidate_no_fly_cells_.size() < 3) {
        candidate_no_fly_cells_.append(cell_code);
    } else {
        status_label_->setText("状态: 已选择 3 个禁飞格，请先取消一个");
        return;
    }

    grid_scene_->setCandidateNoFlyCells(candidate_no_fly_cells_);
    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    planning_state_.updateSelectionValidity(validation.is_valid);
    refreshPlanningButtonText();
    updateStatusForCandidateSelection(validation);
}

void MainWindow::enterNoFlySelectionMode() {
    planning_state_.handlePrimaryButtonClicked();
    candidate_no_fly_cells_.clear();
    grid_scene_->clearCandidateNoFlyCells();
    grid_scene_->setNoFlyCells({});
    grid_scene_->setNoFlyEditEnabled(true);
    refreshPlanningButtonText();
    status_label_->setText("状态: 请选择 3 个横向或纵向连续的禁飞格");
}

void MainWindow::generateMissionPlanFromCandidateSelection() {
    if (current_start_cell_.isEmpty()) {
        status_label_->setText("错误: 当前起飞格未知，无法生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() != 3) {
        status_label_->setText("状态: 请选择 3 个禁飞格后再生成");
        return;
    }

    const auto validation = NoFlyZoneRules::validateSelection(candidate_no_fly_cells_, current_start_cell_);
    if (!validation.is_valid) {
        updateStatusForCandidateSelection(validation);
        return;
    }

    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, candidate_no_fly_cells_);
    if (!result.ok) {
        status_label_->setText(QString("错误: %1").arg(result.error_message));
        return;
    }

    applyMissionPlanResult(result);
}

void MainWindow::applyMissionPlanResult(const MissionPlanResult &result) {
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

    planning_state_.handleGenerationSucceeded();
    refreshPlanningButtonText();
    refreshMissionContextLabels();

    QString persist_error;
    if (persistMissionPlan(result.plan, &persist_error)) {
        status_label_->setText("状态: 航线生成成功，可执行任务");
        return;
    }

    qWarning() << "Failed to persist mission plan:" << persist_error;
    status_label_->setText(QString("错误: 本地航线已更新，但同步模拟器任务计划失败 | %1").arg(persist_error));
}

void MainWindow::refreshMissionContextLabels() {
    const QString case_text = current_case_id_.isEmpty() ? QStringLiteral("未加载") : current_case_id_;
    const QString start_text = current_start_cell_.isEmpty() ? QStringLiteral("未知") : current_start_cell_;
    const QString terminal_text = current_terminal_cell_.isEmpty() ? QStringLiteral("未知") : current_terminal_cell_;
    case_label_->setText(QString("案例: %1 | 起点: %2 | 终点: %3").arg(case_text, start_text, terminal_text));

    if (current_case_id_.isEmpty()) {
        mission_label_->setText("任务: 等待规划");
        return;
    }

    if (current_landing_enabled_) {
        mission_label_->setText(QString("任务: 最短飞行时间 | 斜降落角: %1°").arg(current_descent_angle_deg_, 0, 'f', 1));
    } else {
        mission_label_->setText("任务: 标准巡查");
    }
}

void MainWindow::refreshPlanningButtonText() {
    if (planning_button_ != nullptr) {
        planning_button_->setText(planning_state_.primaryButtonText());
    }
}

bool MainWindow::persistMissionPlan(const MissionPlanData &plan, QString *error_message) const {
    QJsonObject payload;
    payload["message_type"] = "config";
    payload["case_id"] = plan.case_id;
    payload["start_cell"] = plan.start_cell;
    payload["terminal_cell"] = plan.terminal_cell;
    payload["landing_enabled"] = plan.landing_enabled;
    payload["descent_angle_deg"] = plan.descent_angle_deg.has_value()
        ? QJsonValue(plan.descent_angle_deg.value())
        : QJsonValue::Null;
    payload["takeoff_anchor_x_cm"] = plan.takeoff_anchor_x_cm.has_value()
        ? QJsonValue(plan.takeoff_anchor_x_cm.value())
        : QJsonValue::Null;
    payload["takeoff_anchor_y_cm"] = plan.takeoff_anchor_y_cm.has_value()
        ? QJsonValue(plan.takeoff_anchor_y_cm.value())
        : QJsonValue::Null;

    QJsonArray no_fly_cells_json;
    for (const QString &cell_code : plan.no_fly_cells) {
        no_fly_cells_json.append(cell_code);
    }
    payload["no_fly_cells"] = no_fly_cells_json;

    QJsonArray route_json;
    for (const QString &cell_code : plan.route) {
        route_json.append(cell_code);
    }
    payload["route"] = route_json;

    const QFileInfo output_file_info(mission_plan_output_path_);
    QDir().mkpath(output_file_info.absolutePath());

    QSaveFile output_file(output_file_info.filePath());
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    const QByteArray document_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (output_file.write(document_bytes) != document_bytes.size()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        output_file.cancelWriting();
        return false;
    }

    if (!output_file.commit()) {
        if (error_message != nullptr) {
            *error_message = output_file.errorString();
        }
        return false;
    }

    return true;
}

void MainWindow::loadInitialMissionPreview() {
    const auto result = mission_plan_bridge_.generatePlan(case_file_path_, {});
    if (!result.ok) {
        qWarning() << "Failed to load initial mission preview:" << result.error_message;
        status_label_->setText(QString("状态: 默认航线加载失败 | %1").arg(result.error_message));
        return;
    }

    applyMissionPlanResult(result);
    status_label_->setText("状态: 默认航线已加载，可设置禁飞区");
}

QString MainWindow::resolveCaseFilePath(const QString &case_id) const {
    if (case_id.isEmpty()) {
        return case_file_path_;
    }

    const QString candidate_path = QDir(repositoryRootPath()).filePath(QStringLiteral("cases/%1.json").arg(case_id));
    if (QFileInfo::exists(candidate_path)) {
        return candidate_path;
    }
    return case_file_path_;
}

void MainWindow::updateStatusForCandidateSelection(const NoFlyZoneRules::ValidationResult &validation) {
    if (validation.is_valid) {
        status_label_->setText("状态: 已选择 3/3 个禁飞格，可生成航线");
        return;
    }

    if (candidate_no_fly_cells_.size() < 3) {
        status_label_->setText(QString("状态: 已选择 %1/3 个禁飞格").arg(candidate_no_fly_cells_.size()));
        return;
    }

    status_label_->setText(QString("状态: %1").arg(validation.message));
}
