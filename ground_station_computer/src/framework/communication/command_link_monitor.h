#pragma once

#include "framework/communication/command_link_health.h"
#include "framework/communication/reliable_command_client.h"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <memory>

class CommandLinkMonitor final : public QThread {
    Q_OBJECT

public:
    static constexpr int kHeartbeatIntervalMs = 2000;

    explicit CommandLinkMonitor(std::shared_ptr<const CommandTransport> transport,
        int heartbeat_interval_ms = kHeartbeatIntervalMs, QObject *parent = nullptr);
    ~CommandLinkMonitor() override;

    void setActiveTaskId(QString task_id);
    void requestImmediateProbe();
    void recordExternalCommandResult(const CommandSendResult &result);
    void startMonitoring();
    void stopMonitoring();

signals:
    void healthChanged(CommandLinkSnapshot snapshot);

protected:
    void run() override;

private:
    std::shared_ptr<const CommandTransport> transport_;
    int heartbeat_interval_ms_;
    QMutex mutex_;
    QWaitCondition wake_condition_;
    QString active_task_id_;
    bool immediate_probe_requested_ = false;
    CommandLinkHealthTracker tracker_;
};
