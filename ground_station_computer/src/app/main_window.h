#pragma once

#include <QMainWindow>
#include <QPushButton>

#include "framework/communication/reliable_command_client.h"
#include "framework/communication/zmq_command_client.h"
#include "framework/task/competition_task_adapter.h"
#include "h_problem/mission/h_mission_command_service.h"

#include <memory>

class QLabel;
class QTimer;
class ZmqSubscriberWorker;

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
    void handlePlanningButtonClicked();
    void handleExecuteMissionClicked();
    void handleStopMissionClicked();
    void handleArmVisionClicked();
    void handleProbeAirborneLinkClicked();
    void sendManualVisionArmCommand();

    void probeAirborneAvailability(bool update_status_message = false);
    void recordCommandLinkResult(bool online);
    bool commandLinkHealthy() const;
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
    bool command_link_online_ = false;
    qint64 last_successful_command_reply_ms_ = 0;
    QTimer *command_health_expiry_timer_ = nullptr;
    bool telemetry_online_ = false;
    ZmqCommandClient command_client_;
    std::unique_ptr<ZmqCommandTransport> command_transport_;
    std::unique_ptr<ReliableCommandClient> reliable_command_client_;
    std::unique_ptr<MissionCommandService> mission_command_service_;

    ZmqSubscriberWorker *worker_ = nullptr;
};
