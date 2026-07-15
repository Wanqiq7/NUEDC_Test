#include "framework/communication/command_link_monitor.h"

#include <QDebug>
#include <QMetaType>
#include <QMutexLocker>

#include <utility>

CommandLinkMonitor::CommandLinkMonitor(std::shared_ptr<const CommandTransport> transport,
    int heartbeat_interval_ms, QObject *parent)
    : QThread(parent),
      transport_(std::move(transport)),
      heartbeat_interval_ms_(heartbeat_interval_ms > 0 ? heartbeat_interval_ms : kHeartbeatIntervalMs) {
    qRegisterMetaType<CommandLinkSnapshot>("CommandLinkSnapshot");
}

CommandLinkMonitor::~CommandLinkMonitor() {
    stopMonitoring();
}

void CommandLinkMonitor::setActiveTaskId(QString task_id) {
    QMutexLocker lock(&mutex_);
    active_task_id_ = std::move(task_id);
}

void CommandLinkMonitor::requestImmediateProbe() {
    QMutexLocker lock(&mutex_);
    immediate_probe_requested_ = true;
    wake_condition_.wakeOne();
}

void CommandLinkMonitor::recordExternalCommandResult(const CommandSendResult &result) {
    CommandLinkSnapshot snapshot;
    {
        QMutexLocker lock(&mutex_);
        ++health_generation_;
        snapshot = result.ok ? tracker_.recordSuccess(result.message) : tracker_.recordFailure(result.message);
    }
    emit healthChanged(snapshot);
}

void CommandLinkMonitor::startMonitoring() {
    qRegisterMetaType<CommandLinkSnapshot>("CommandLinkSnapshot");
    if (!isRunning()) {
        start();
    }
}

void CommandLinkMonitor::stopMonitoring() {
    requestInterruption();
    {
        QMutexLocker lock(&mutex_);
        wake_condition_.wakeAll();
    }
    if (QThread::currentThread() != this && !wait(3000)) {
        qWarning() << "CommandLinkMonitor shutdown grace period expired; waiting for monitor thread";
        wait();
    }
}

void CommandLinkMonitor::run() {
    ReliableCommandClient reliable_client(transport_.get());

    while (!isInterruptionRequested()) {
        QString task_id;
        quint64 probe_generation = 0;
        {
            QMutexLocker lock(&mutex_);
            if (!immediate_probe_requested_ && !isInterruptionRequested()) {
                wake_condition_.wait(&mutex_, static_cast<unsigned long>(heartbeat_interval_ms_));
            }
            if (isInterruptionRequested()) {
                break;
            }
            immediate_probe_requested_ = false;
            task_id = active_task_id_;
            probe_generation = health_generation_;
        }

        const CommandSendResult result = reliable_client.ping(task_id);
        CommandLinkSnapshot snapshot;
        {
            QMutexLocker lock(&mutex_);
            if (probe_generation != health_generation_) {
                continue;
            }
            snapshot = result.ok ? tracker_.recordSuccess(result.message) : tracker_.recordFailure(result.message);
        }
        emit healthChanged(snapshot);
    }
}
