#pragma once

#include <QDateTime>
#include <QMainWindow>
#include <QPushButton>

#include "framework/communication/reliable_command_client.h"
#include "framework/communication/command_link_health.h"
#include "framework/communication/command_link_monitor.h"
#include "framework/communication/serialized_command_transport.h"
#include "framework/communication/zmq_command_client.h"
#include "framework/task/competition_task_adapter.h"
#include "h_problem/mission/h_mission_command_service.h"

#include <memory>

class QLabel;
class ZmqSubscriberWorker;
class MainWindowTests;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool start_worker = true);
    ~MainWindow() override;

private slots:
    void handleTaskPlan(competition::TaskPlan plan);
    void handleTaskEvent(competition::TaskEvent event, qint64 timestamp_ms);
    void handleTaskSummary(competition::TaskSummary summary);
    void handleError(QString message);

private:
    friend class MainWindowTests;

    static constexpr qint64 kTelemetryLinkTtlMs = 5000;

    void handlePlanningButtonClicked();
    void handleExecuteMissionClicked();
    void handleStopMissionClicked();
    void handleArmVisionClicked();
    void handleProbeAirborneLinkClicked();
    void sendManualVisionArmCommand();

    void handleCommandLinkHealthChanged(CommandLinkSnapshot snapshot);
    bool commandLinkHealthy() const;
    void recordTelemetryReceived();
    bool telemetryLinkHealthy(qint64 now_ms = QDateTime::currentMSecsSinceEpoch()) const;
    bool telemetryLinkHealthyAt(qint64 now_ms) const;
    QString telemetryStatusTextAt(qint64 now_ms) const;
    void refreshExecutionControls();
    void refreshAirborneStatusLabel();

    std::unique_ptr<CompetitionTaskAdapter> task_adapter_;
    QLabel *status_label_ = nullptr;

    QPushButton *planning_button_ = nullptr;
    QPushButton *execute_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QPushButton *arm_vision_button_ = nullptr;
    QPushButton *probe_airborne_link_button_ = nullptr;
    QLabel *airborne_status_label_ = nullptr;
    bool command_sync_enabled_ = true;
    CommandLinkSnapshot command_link_snapshot_;
    qint64 last_successful_telemetry_ms_ = 0;
    ZmqCommandClient command_client_;
    std::shared_ptr<SerializedCommandTransport> command_transport_;
    std::unique_ptr<MissionCommandService> mission_command_service_;
    std::unique_ptr<CommandLinkMonitor> command_link_monitor_;

    ZmqSubscriberWorker *worker_ = nullptr;
};
