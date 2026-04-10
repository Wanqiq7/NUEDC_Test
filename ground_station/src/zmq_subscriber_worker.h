#pragma once

#include <QMap>
#include <QThread>

class ZmqSubscriberWorker : public QThread {
    Q_OBJECT

public:
    explicit ZmqSubscriberWorker(QString endpoint, QObject *parent = nullptr);

signals:
    void gridConfigReceived(
        QString case_id,
        QString start_cell,
        QStringList no_fly_cells,
        QStringList route,
        QString terminal_cell,
        bool landing_enabled,
        double descent_angle_deg,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm);
    void telemetryReceived(QString current_cell, int step_index, int visited_cells);
    void detectionReceived(QString cell_code, QString animal_name, int count, qint64 timestamp_ms);
    void summaryReceived(QMap<QString, int> totals, int visited_cells);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    QString endpoint_;
};
