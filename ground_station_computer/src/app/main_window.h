#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QStringList>

#include "framework/communication/zmq_command_client.h"
#include "framework/task/competition_task_adapter.h"

#include <memory>

class QLabel;
class QListWidget;
class QTableWidget;
class ZmqSubscriberWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool start_worker = true);
    ~MainWindow() override;

private slots:
    void handleGridConfig(
        QString case_id,
        QString start_cell,
        QStringList no_fly_cells,
        QStringList route,
        QString terminal_cell,
        bool landing_enabled,
        double descent_angle_deg,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm);
    void handleTelemetry(QString current_cell, int step_index, int visited_cells);
    void handleDetection(QString cell_code, QString animal_name, int count, qint64 timestamp_ms);
    void handleSummary(QMap<QString, int> totals, int visited_cells);
    void handleError(QString message);

private:
    void handlePlanningButtonClicked();
    void handleExecuteMissionClicked();
    void handleStopMissionClicked();

    void probeAirborneAvailability(bool update_status_message = false);
    void refreshExecutionControls();
    void refreshAirborneStatusLabel();

    void updateSummaryTable(const QMap<QString, int> &totals);

    std::unique_ptr<CompetitionTaskAdapter> task_adapter_;
    QLabel *status_label_ = nullptr;
    QLabel *case_label_ = nullptr;
    QLabel *mission_label_ = nullptr;
    QLabel *legend_label_ = nullptr;
    QListWidget *detection_list_ = nullptr;
    QTableWidget *summary_table_ = nullptr;

    QPushButton *planning_button_ = nullptr;
    QPushButton *execute_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *airborne_status_label_ = nullptr;
    bool command_sync_enabled_ = true;
    bool airborne_online_ = false;
    ZmqCommandClient command_client_;

    ZmqSubscriberWorker *worker_ = nullptr;
};
