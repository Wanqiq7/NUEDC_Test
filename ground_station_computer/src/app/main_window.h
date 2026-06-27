#pragma once

#include <QMainWindow>
#include <QPushButton>

#include "framework/communication/reliable_command_client.h"
#include "framework/communication/zmq_command_client.h"
#include "framework/task/competition_task_adapter.h"

#include <memory>

class QLabel;
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

    void probeAirborneAvailability(bool update_status_message = false);
    void refreshExecutionControls();
    void refreshAirborneStatusLabel();

    std::unique_ptr<CompetitionTaskAdapter> task_adapter_;
    QLabel *status_label_ = nullptr;

    QPushButton *planning_button_ = nullptr;
    QPushButton *execute_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *airborne_status_label_ = nullptr;
    bool command_sync_enabled_ = true;
    bool airborne_online_ = false;
    ZmqCommandClient command_client_;
    std::unique_ptr<ZmqCommandTransport> command_transport_;
    std::unique_ptr<ReliableCommandClient> reliable_command_client_;

    ZmqSubscriberWorker *worker_ = nullptr;
};
