#include <QtTest/QtTest>

#include "framework/communication/command_link_health.h"

class CommandLinkHealthTests : public QObject {
    Q_OBJECT

private slots:
    void startsChecking();
    void marksOfflineOnlyAfterThirdConsecutiveFailure();
    void successImmediatelyRestoresOnlineAndClearsFailures();
    void invalidThresholdFallsBackToThreeFailures();
};

void CommandLinkHealthTests::startsChecking() {
    CommandLinkHealthTracker tracker;
    const CommandLinkSnapshot snapshot = tracker.snapshot();
    QCOMPARE(snapshot.health, CommandLinkHealth::Checking);
    QCOMPARE(snapshot.consecutive_failures, 0);
}

void CommandLinkHealthTests::marksOfflineOnlyAfterThirdConsecutiveFailure() {
    CommandLinkHealthTracker tracker;
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    const CommandLinkSnapshot third = tracker.recordFailure("timeout");
    QCOMPARE(third.health, CommandLinkHealth::Offline);
    QCOMPARE(third.consecutive_failures, 3);
    QCOMPARE(third.detail, QString("timeout"));
}

void CommandLinkHealthTests::successImmediatelyRestoresOnlineAndClearsFailures() {
    CommandLinkHealthTracker tracker;
    tracker.recordFailure("first timeout");
    tracker.recordFailure("second timeout");
    const CommandLinkSnapshot snapshot = tracker.recordSuccess("pong");
    QCOMPARE(snapshot.health, CommandLinkHealth::Online);
    QCOMPARE(snapshot.consecutive_failures, 0);
    QCOMPARE(snapshot.detail, QString("pong"));
}

void CommandLinkHealthTests::invalidThresholdFallsBackToThreeFailures() {
    CommandLinkHealthTracker tracker(0);
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    const CommandLinkSnapshot third = tracker.recordFailure("timeout");
    QCOMPARE(third.health, CommandLinkHealth::Offline);
    QCOMPARE(third.consecutive_failures, 3);
}

QTEST_MAIN(CommandLinkHealthTests)
#include "test_command_link_health.moc"
