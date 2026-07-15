#include <QtTest/QtTest>

#include "framework/communication/command_link_monitor.h"
#include "framework/communication/serialized_command_transport.h"
#include "messages.pb.h"

#include <QSignalSpy>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct QueueTransportState {
    std::mutex mutex;
    std::vector<CommandSendResult> results;
    std::atomic<int> current_sends{0};
    std::atomic<int> maximum_sends{0};
    int send_delay_ms = 0;
};

class QueueTransport final : public CommandTransport {
public:
    explicit QueueTransport(std::shared_ptr<QueueTransportState> state)
        : state_(std::move(state)) {}

    CommandSendResult sendEnvelope(const Envelope &) const override {
        const int concurrent_sends = state_->current_sends.fetch_add(1) + 1;
        int observed_maximum = state_->maximum_sends.load();
        while (concurrent_sends > observed_maximum
               && !state_->maximum_sends.compare_exchange_weak(observed_maximum, concurrent_sends)) {
        }

        if (state_->send_delay_ms > 0) {
            QThread::msleep(static_cast<unsigned long>(state_->send_delay_ms));
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->current_sends.fetch_sub(1);
        if (state_->results.empty()) {
            return CommandSendResult{false, "unexpected send"};
        }
        const CommandSendResult result = state_->results.front();
        state_->results.erase(state_->results.begin());
        return result;
    }

private:
    std::shared_ptr<QueueTransportState> state_;
};

struct BlockingTransportState {
    std::mutex mutex;
    std::condition_variable condition;
    bool send_started = false;
    bool release_send = false;
    int completed_sends = 0;
    CommandSendResult result{true, "pong"};
};

class BlockingTransport final : public CommandTransport {
public:
    explicit BlockingTransport(std::shared_ptr<BlockingTransportState> state)
        : state_(std::move(state)) {}

    CommandSendResult sendEnvelope(const Envelope &) const override {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->send_started = true;
        state_->condition.notify_all();
        state_->condition.wait(lock, [this] { return state_->release_send; });
        ++state_->completed_sends;
        state_->condition.notify_all();
        return state_->result;
    }

private:
    std::shared_ptr<BlockingTransportState> state_;
};

std::shared_ptr<CommandTransport> makeTransport(const std::shared_ptr<QueueTransportState> &state) {
    return std::make_shared<SerializedCommandTransport>(std::make_unique<QueueTransport>(state));
}

void enqueueFailures(const std::shared_ptr<QueueTransportState> &state, int count) {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (int index = 0; index < count; ++index) {
        state->results.push_back(CommandSendResult{false, "timeout"});
    }
}

void enqueueResult(const std::shared_ptr<QueueTransportState> &state, CommandSendResult result) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->results.push_back(std::move(result));
}

void waitForBlockedSend(const std::shared_ptr<BlockingTransportState> &state) {
    std::unique_lock<std::mutex> lock(state->mutex);
    const bool send_started = state->condition.wait_for(lock, std::chrono::seconds(1), [state] {
        return state->send_started;
    });
    QVERIFY(send_started);
}

void releaseBlockedSend(const std::shared_ptr<BlockingTransportState> &state) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release_send = true;
    state->condition.notify_all();
}

void waitForCompletedSends(const std::shared_ptr<BlockingTransportState> &state, int expected_sends) {
    std::unique_lock<std::mutex> lock(state->mutex);
    const bool completed = state->condition.wait_for(lock, std::chrono::seconds(1), [state, expected_sends] {
        return state->completed_sends >= expected_sends;
    });
    QVERIFY(completed);
}

} // namespace

class CommandLinkMonitorTests : public QObject {
    Q_OBJECT

private slots:
    void doesNotReportOfflineForFirstTwoHeartbeatFailures();
    void reportsOfflineAfterThirdHeartbeatFailure();
    void successfulHeartbeatRecoversFromOffline();
    void externalSuccessfulCommandResetsFailureCount();
    void staleHeartbeatFailureCannotOverwriteNewerExternalSuccess();
    void neverOverlapsHeartbeatAndExternalTransportSend();
    void stopMonitoringJoinsAfterBlockedTransportSend();
};

void CommandLinkMonitorTests::doesNotReportOfflineForFirstTwoHeartbeatFailures() {
    const auto state = std::make_shared<QueueTransportState>();
    enqueueFailures(state, 6);
    const auto transport = makeTransport(state);
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.setActiveTaskId("task-001");
    monitor.startMonitoring();
    monitor.requestImmediateProbe();
    QTRY_COMPARE(spy.size(), 1);
    monitor.requestImmediateProbe();
    QTRY_COMPARE(spy.size(), 2);

    const auto first = spy.at(0).at(0).value<CommandLinkSnapshot>();
    const auto second = spy.at(1).at(0).value<CommandLinkSnapshot>();
    QCOMPARE(first.health, CommandLinkHealth::Checking);
    QCOMPARE(second.health, CommandLinkHealth::Checking);
    QCOMPARE(second.consecutive_failures, 2);
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::reportsOfflineAfterThirdHeartbeatFailure() {
    const auto state = std::make_shared<QueueTransportState>();
    enqueueFailures(state, 9);
    const auto transport = makeTransport(state);
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.startMonitoring();
    for (int index = 0; index < 3; ++index) {
        monitor.requestImmediateProbe();
        QTRY_COMPARE(spy.size(), index + 1);
    }

    QCOMPARE(spy.at(0).at(0).value<CommandLinkSnapshot>().health, CommandLinkHealth::Checking);
    QCOMPARE(spy.at(1).at(0).value<CommandLinkSnapshot>().health, CommandLinkHealth::Checking);
    const auto third = spy.at(2).at(0).value<CommandLinkSnapshot>();
    QCOMPARE(third.health, CommandLinkHealth::Offline);
    QCOMPARE(third.consecutive_failures, 3);
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::successfulHeartbeatRecoversFromOffline() {
    const auto state = std::make_shared<QueueTransportState>();
    enqueueFailures(state, 9);
    enqueueResult(state, CommandSendResult{true, "pong"});
    const auto transport = makeTransport(state);
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.startMonitoring();
    for (int index = 0; index < 4; ++index) {
        monitor.requestImmediateProbe();
        QTRY_COMPARE(spy.size(), index + 1);
    }

    const auto recovery = spy.at(3).at(0).value<CommandLinkSnapshot>();
    QCOMPARE(recovery.health, CommandLinkHealth::Online);
    QCOMPARE(recovery.consecutive_failures, 0);
    QCOMPARE(recovery.detail, QString("pong"));
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::externalSuccessfulCommandResetsFailureCount() {
    const auto state = std::make_shared<QueueTransportState>();
    enqueueFailures(state, 6);
    const auto transport = makeTransport(state);
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.startMonitoring();
    for (int index = 0; index < 2; ++index) {
        monitor.requestImmediateProbe();
        QTRY_COMPARE(spy.size(), index + 1);
    }
    monitor.recordExternalCommandResult(CommandSendResult{true, "mission started"});
    QTRY_COMPARE(spy.size(), 3);

    const auto snapshot = spy.at(2).at(0).value<CommandLinkSnapshot>();
    QCOMPARE(snapshot.health, CommandLinkHealth::Online);
    QCOMPARE(snapshot.consecutive_failures, 0);
    QCOMPARE(snapshot.detail, QString("mission started"));
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::staleHeartbeatFailureCannotOverwriteNewerExternalSuccess() {
    const auto state = std::make_shared<BlockingTransportState>();
    state->result = CommandSendResult{false, "heartbeat timed out"};
    const auto transport = std::make_shared<SerializedCommandTransport>(std::make_unique<BlockingTransport>(state));
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.startMonitoring();
    monitor.requestImmediateProbe();
    waitForBlockedSend(state);

    monitor.recordExternalCommandResult(CommandSendResult{true, "mission started"});
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).value<CommandLinkSnapshot>().health, CommandLinkHealth::Online);

    releaseBlockedSend(state);
    waitForCompletedSends(state, 3);
    QTest::qWait(50);

    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).value<CommandLinkSnapshot>().health, CommandLinkHealth::Online);
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::neverOverlapsHeartbeatAndExternalTransportSend() {
    const auto state = std::make_shared<QueueTransportState>();
    state->send_delay_ms = 150;
    enqueueResult(state, CommandSendResult{true, "pong"});
    enqueueResult(state, CommandSendResult{true, "external success"});
    const auto transport = makeTransport(state);
    CommandLinkMonitor monitor(transport, 60000);
    QSignalSpy spy(&monitor, &CommandLinkMonitor::healthChanged);

    monitor.startMonitoring();
    monitor.requestImmediateProbe();
    QTRY_VERIFY(state->current_sends.load() == 1);
    std::thread external_sender([transport] {
        Envelope envelope;
        transport->sendEnvelope(envelope);
    });
    QTRY_COMPARE(spy.size(), 1);
    external_sender.join();

    QCOMPARE(state->maximum_sends.load(), 1);
    monitor.stopMonitoring();
}

void CommandLinkMonitorTests::stopMonitoringJoinsAfterBlockedTransportSend() {
    const auto state = std::make_shared<BlockingTransportState>();
    const auto transport = std::make_shared<SerializedCommandTransport>(std::make_unique<BlockingTransport>(state));
    CommandLinkMonitor monitor(transport, 60000);

    monitor.startMonitoring();
    monitor.requestImmediateProbe();
    waitForBlockedSend(state);

    std::atomic<bool> stop_returned{false};
    std::thread stopper([&monitor, &stop_returned] {
        monitor.stopMonitoring();
        stop_returned.store(true);
    });

    QTest::qWait(3200);
    const bool returned_before_release = stop_returned.load();
    releaseBlockedSend(state);
    stopper.join();

    QVERIFY(!returned_before_release);
    QVERIFY(stop_returned.load());
    QVERIFY(!monitor.isRunning());
}

QTEST_MAIN(CommandLinkMonitorTests)
#include "test_command_link_monitor.moc"
