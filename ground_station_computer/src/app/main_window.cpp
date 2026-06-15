#include "app/main_window.h"

#include "framework/config/network_config.h"
#include "framework/runtime/mission_runtime_state.h"
#include "messages.pb.h"
#include "framework/communication/zmq_subscriber_worker.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent, bool start_worker)
    : QMainWindow(parent),
      task_adapter_(createDefaultCompetitionTaskAdapter()) {
    const NetworkConfig network_config = NetworkConfig::fromEnvironment();
    command_sync_enabled_ = start_worker;
    command_client_ = ZmqCommandClient(network_config.commandEndpoint());
    command_transport_ = std::make_unique<ZmqCommandTransport>(command_client_);
    reliable_command_client_ = std::make_unique<ReliableCommandClient>(command_transport_.get());
    task_adapter_->setCommandSyncEnabled(command_sync_enabled_);
    task_adapter_->setCommandClient(command_client_);

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

    auto *preview_title = new QLabel("航线预览", this);
    preview_title->setObjectName("PanelTitle");
    auto *preview_subtitle = new QLabel("起飞前先展示整条路线，蓝线表示主航线，橙色高亮带强调重复航段。", this);
    preview_subtitle->setObjectName("PanelSubtitle");
    preview_subtitle->setWordWrap(true);
    left_layout->addWidget(preview_title);
    left_layout->addWidget(preview_subtitle);
    left_layout->addWidget(task_adapter_->createTaskView(this), 1);

    planning_button_ = new QPushButton(task_adapter_->initialPlanningButtonText(), this);
    planning_button_->setObjectName("PlanningButton");
    planning_button_->setFixedHeight(46);
    planning_button_->setCursor(Qt::PointingHandCursor);
    left_layout->addWidget(planning_button_);

    airborne_status_label_ = new QLabel("机载状态: 检查中", this);
    airborne_status_label_->setObjectName("AirborneStatusLabel");
    airborne_status_label_->setStyleSheet("color: #475569; font-size: 13px; padding: 2px 0;");
    left_layout->addWidget(airborne_status_label_);

    auto *action_layout = new QHBoxLayout();
    action_layout->setSpacing(10);
    execute_button_ = new QPushButton("执行任务", this);
    execute_button_->setObjectName("ExecuteMissionButton");
    execute_button_->setCursor(Qt::PointingHandCursor);
    stop_button_ = new QPushButton("停止任务", this);
    stop_button_->setObjectName("StopMissionButton");
    stop_button_->setCursor(Qt::PointingHandCursor);
    action_layout->addWidget(execute_button_);
    action_layout->addWidget(stop_button_);
    left_layout->addLayout(action_layout);

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

    task_adapter_->setStatusTextCallback([this](const QString &text) {
        status_label_->setText(text);
    });
    task_adapter_->setCaseTextCallback([this](const QString &text) {
        case_label_->setText(text);
    });
    task_adapter_->setMissionTextCallback([this](const QString &text) {
        mission_label_->setText(text);
    });
    task_adapter_->setPlanningButtonTextCallback([this](const QString &text) {
        planning_button_->setText(text);
    });
    task_adapter_->setRuntimeCallback([this](bool, bool) {
        refreshExecutionControls();
    });
    task_adapter_->setDetectionCallback([this](const QString &text) {
        detection_list_->addItem(text);
    });
    task_adapter_->setSummaryCallback([this](const QMap<QString, int> &totals) {
        updateSummaryTable(totals);
    });

    connect(planning_button_, &QPushButton::clicked, this, &MainWindow::handlePlanningButtonClicked);
    connect(execute_button_, &QPushButton::clicked, this, &MainWindow::handleExecuteMissionClicked);
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::handleStopMissionClicked);
    task_adapter_->loadInitialPreview();
    refreshExecutionControls();
    refreshAirborneStatusLabel();
    probeAirborneAvailability(false);

    worker_ = new ZmqSubscriberWorker(network_config.telemetryEndpoint(), this);
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
    airborne_online_ = true;
    task_adapter_->handleGridConfig(TaskGridConfig{
        case_id,
        start_cell,
        no_fly_cells,
        route,
        terminal_cell,
        landing_enabled,
        descent_angle_deg,
        takeoff_anchor_x_cm,
        takeoff_anchor_y_cm,
    });
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleTelemetry(QString current_cell, int step_index, int visited_cells) {
    airborne_online_ = true;
    task_adapter_->handleTelemetry(current_cell, step_index, visited_cells);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleDetection(QString cell_code, QString animal_name, int count, qint64 timestamp_ms) {
    task_adapter_->handleDetection(cell_code, animal_name, count, timestamp_ms);
}

void MainWindow::handleSummary(QMap<QString, int> totals, int visited_cells) {
    airborne_online_ = true;
    task_adapter_->handleSummary(totals, visited_cells);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleError(QString message) {
    status_label_->setText(QString("错误: %1").arg(message));
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
    task_adapter_->handlePlanningButtonClicked();
}

void MainWindow::handleExecuteMissionClicked() {
    if (!command_sync_enabled_) {
        status_label_->setText("状态: 测试模式不支持执行控制");
        return;
    }
    if (!task_adapter_->missionSyncedToAirborne()) {
        status_label_->setText("状态: 请先生成并同步任务到机载端");
        return;
    }

    const auto result = reliable_command_client_->sendReliable(
        ZmqCommandClient::buildControlCommandEnvelope(
            GroundControlCommandType::StartMission,
            task_adapter_->activeTaskId()));
    airborne_online_ = result.ok;
    if (!result.ok) {
        task_adapter_->markAirborneSyncState(false, task_adapter_->missionSyncedToAirborne());
        refreshAirborneStatusLabel();
        refreshExecutionControls();
        status_label_->setText(QString("错误: 开始执行命令失败 | %1").arg(result.message));
        return;
    }

    task_adapter_->markControlCommandStarted();
    refreshAirborneStatusLabel();
    refreshExecutionControls();
    status_label_->setText("状态: 已发送开始执行命令");
}

void MainWindow::handleStopMissionClicked() {
    if (!command_sync_enabled_) {
        status_label_->setText("状态: 测试模式不支持执行控制");
        return;
    }

    const auto result = reliable_command_client_->sendReliable(
        ZmqCommandClient::buildControlCommandEnvelope(
            GroundControlCommandType::StopMission,
            task_adapter_->activeTaskId()));
    airborne_online_ = result.ok;
    if (!result.ok) {
        refreshAirborneStatusLabel();
        refreshExecutionControls();
        status_label_->setText(QString("错误: 停止执行命令失败 | %1").arg(result.message));
        return;
    }

    task_adapter_->markControlCommandStopped();
    refreshAirborneStatusLabel();
    refreshExecutionControls();
    status_label_->setText("状态: 已发送停止执行命令");
}

void MainWindow::probeAirborneAvailability(bool update_status_message) {
    if (!command_sync_enabled_) {
        airborne_online_ = false;
        refreshAirborneStatusLabel();
        refreshExecutionControls();
        return;
    }

    const auto result = reliable_command_client_->ping(task_adapter_->activeTaskId());
    airborne_online_ = result.ok;
    refreshAirborneStatusLabel();
    refreshExecutionControls();
    if (update_status_message) {
        status_label_->setText(
            result.ok
                ? QString("状态: 机载端在线 | %1").arg(result.message)
                : QString("状态: 机载端离线 | %1").arg(result.message));
    }
}

void MainWindow::refreshExecutionControls() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        command_sync_enabled_,
        airborne_online_,
        task_adapter_->missionSyncedToAirborne(),
        task_adapter_->missionRunning(),
    });
    if (execute_button_ != nullptr) {
        execute_button_->setEnabled(controls.can_execute);
    }
    if (stop_button_ != nullptr) {
        stop_button_->setEnabled(controls.can_stop);
    }
}

void MainWindow::refreshAirborneStatusLabel() {
    if (airborne_status_label_ == nullptr) {
        return;
    }
    if (!command_sync_enabled_) {
        airborne_status_label_->setText("机载状态: 测试模式");
        return;
    }
    airborne_status_label_->setText(airborne_online_ ? "机载状态: 在线" : "机载状态: 离线");
}
