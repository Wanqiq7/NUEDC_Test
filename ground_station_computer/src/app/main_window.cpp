#include "app/main_window.h"

#include "framework/config/network_config.h"
#include "framework/runtime/mission_runtime_state.h"
#include "framework/communication/zmq_subscriber_worker.h"
#include "messages.pb.h"

#include <QDebug>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent, bool start_worker)
    : QMainWindow(parent) {
    QString adapter_error;
    task_adapter_ = createConfiguredCompetitionTaskAdapter(&adapter_error);
    if (!adapter_error.isEmpty()) {
        qWarning() << "Failed to create configured task adapter:" << adapter_error;
    }
    if (task_adapter_ == nullptr) {
        qWarning() << "Falling back to default task adapter";
        task_adapter_ = createDefaultCompetitionTaskAdapter();
    }
    const NetworkConfig network_config = NetworkConfig::fromEnvironment();
    command_sync_enabled_ = start_worker;
    command_client_ = ZmqCommandClient(network_config.commandEndpoint());
    command_transport_ = std::make_unique<ZmqCommandTransport>(command_client_);
    reliable_command_client_ = std::make_unique<ReliableCommandClient>(command_transport_.get());
    task_adapter_->setCommandSyncEnabled(command_sync_enabled_);
    task_adapter_->setCommandClient(command_client_);

    auto *central = new QWidget(this);
    auto *root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(18, 18, 18, 18);
    root_layout->setSpacing(10);

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
        "QAbstractItemView {"
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

    auto *preview_title = new QLabel("任务视图", this);
    preview_title->setObjectName("PanelTitle");
    auto *preview_subtitle = new QLabel("具体任务页面由当前题目 Adapter 提供，Shell 只负责通用连接与执行控制。", this);
    preview_subtitle->setObjectName("PanelSubtitle");
    preview_subtitle->setWordWrap(true);
    root_layout->addWidget(preview_title);
    root_layout->addWidget(preview_subtitle);
    root_layout->addWidget(task_adapter_->createTaskView(this), 1);

    planning_button_ = new QPushButton(task_adapter_->initialPlanningButtonText(), this);
    planning_button_->setObjectName("PlanningButton");
    planning_button_->setFixedHeight(46);
    planning_button_->setCursor(Qt::PointingHandCursor);
    root_layout->addWidget(planning_button_);

    airborne_status_label_ = new QLabel("机载状态: 检查中", this);
    airborne_status_label_->setObjectName("AirborneStatusLabel");
    airborne_status_label_->setStyleSheet("color: #475569; font-size: 13px; padding: 2px 0;");
    root_layout->addWidget(airborne_status_label_);

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
    root_layout->addLayout(action_layout);

    status_label_ = new QLabel("状态: 等待消息", this);
    status_label_->setObjectName("StatusText");
    root_layout->addWidget(status_label_);

    setCentralWidget(central);
    resize(1100, 620);
    setWindowTitle("无人机竞赛地面站");

    task_adapter_->setStatusTextCallback([this](const QString &text) {
        status_label_->setText(text);
    });
    task_adapter_->setPlanningButtonTextCallback([this](const QString &text) {
        planning_button_->setText(text);
    });
    task_adapter_->setRuntimeCallback([this]() {
        refreshAirborneStatusLabel();
        refreshExecutionControls();
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
        connect(worker_, &ZmqSubscriberWorker::taskPlanReceived, this, &MainWindow::handleTaskPlan);
        connect(worker_, &ZmqSubscriberWorker::taskEventReceived, this, &MainWindow::handleTaskEvent);
        connect(worker_, &ZmqSubscriberWorker::taskSummaryReceived, this, &MainWindow::handleTaskSummary);
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

void MainWindow::handleTaskPlan(competition::TaskPlan plan) {
    airborne_online_ = true;
    task_adapter_->handleTaskPlan(plan);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleTaskEvent(competition::TaskEvent event, qint64 timestamp_ms) {
    airborne_online_ = true;
    task_adapter_->handleTaskEvent(event, timestamp_ms);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleTaskSummary(competition::TaskSummary summary) {
    airborne_online_ = true;
    task_adapter_->handleTaskSummary(summary);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}

void MainWindow::handleError(QString message) {
    status_label_->setText(QString("错误: %1").arg(message));
}

void MainWindow::handlePlanningButtonClicked() {
    task_adapter_->handlePlanningButtonClicked();
}

void MainWindow::handleExecuteMissionClicked() {
    if (!command_sync_enabled_) {
        status_label_->setText("状态: 测试模式不支持执行控制");
        return;
    }
    MissionRuntimeInputs inputs = task_adapter_->missionRuntimeInputs();
    inputs.command_sync_enabled = command_sync_enabled_;
    inputs.airborne_online = airborne_online_;
    if (!MissionRuntimeState::controlsFor(inputs).can_execute) {
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
        status_label_->setText(ReliableCommandClient::operatorStatusText("开始执行命令", result));
        return;
    }

    task_adapter_->applyCommandAck(result);
    if (result.task_id.isEmpty() && result.last_accepted_sequence == 0) {
        task_adapter_->markControlCommandStarted();
    }
    refreshAirborneStatusLabel();
    refreshExecutionControls();
    status_label_->setText(ReliableCommandClient::operatorStatusText("开始执行命令", result));
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
        status_label_->setText(ReliableCommandClient::operatorStatusText("停止执行命令", result));
        return;
    }

    task_adapter_->applyCommandAck(result);
    if (result.task_id.isEmpty() && result.last_accepted_sequence == 0) {
        task_adapter_->markControlCommandStopped();
    }
    refreshAirborneStatusLabel();
    refreshExecutionControls();
    status_label_->setText(ReliableCommandClient::operatorStatusText("停止执行命令", result));
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
    task_adapter_->applyCommandAck(result);
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
    MissionRuntimeInputs inputs = task_adapter_->missionRuntimeInputs();
    inputs.command_sync_enabled = command_sync_enabled_;
    inputs.airborne_online = airborne_online_;
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(inputs);
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

    MissionRuntimeInputs inputs = task_adapter_->missionRuntimeInputs();
    inputs.command_sync_enabled = command_sync_enabled_;
    inputs.airborne_online = airborne_online_;
    airborne_status_label_->setText(MissionRuntimeState::airborneStatusText(inputs));
}

