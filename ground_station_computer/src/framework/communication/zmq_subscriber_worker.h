#pragma once

#include "competition_core/task/models.h"

#include <QThread>

class ZmqSubscriberWorker : public QThread {
    Q_OBJECT

public:
    explicit ZmqSubscriberWorker(QString endpoint, QObject *parent = nullptr);

signals:
    void taskPlanReceived(competition::TaskPlan plan);
    void taskEventReceived(competition::TaskEvent event, qint64 timestamp_ms);
    void taskSummaryReceived(competition::TaskSummary summary);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    QString endpoint_;
};
