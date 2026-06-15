#include <QtTest/QtTest>

#include "framework/communication/reliable_command_client.h"
#include "messages.pb.h"

namespace {

class ScriptedTransport final : public CommandTransport {
public:
    explicit ScriptedTransport(QVector<CommandSendResult> replies)
        : replies_(std::move(replies)) {}

    CommandSendResult sendEnvelope(const Envelope &) const override {
        ++attempts_;
        if (attempts_ <= replies_.size()) {
            return replies_.at(attempts_ - 1);
        }
        return CommandSendResult{false, "unexpected extra attempt"};
    }

    int attempts() const { return attempts_; }

private:
    QVector<CommandSendResult> replies_;
    mutable int attempts_ = 0;
};

}

class ReliableCommandClientTests : public QObject {
    Q_OBJECT

private slots:
    void retriesUntilCommandSucceeds();
    void stopsAfterConfiguredAttempts();
    void heartbeatCachesOnlineState();
};

void ReliableCommandClientTests::retriesUntilCommandSucceeds() {
    ScriptedTransport transport({
        CommandSendResult{false, "timeout"},
        CommandSendResult{true, "task plan stored"},
    });
    ReliableCommandClient client(&transport, ReliableCommandPolicy{3, 0});

    Envelope envelope;
    const CommandSendResult result = client.sendReliable(envelope);

    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("task plan stored"));
    QCOMPARE(transport.attempts(), 2);
    QCOMPARE(static_cast<int>(client.status()), static_cast<int>(CommandLinkStatus::Connected));
}

void ReliableCommandClientTests::stopsAfterConfiguredAttempts() {
    ScriptedTransport transport({
        CommandSendResult{false, "timeout"},
        CommandSendResult{false, "still offline"},
        CommandSendResult{true, "late success"},
    });
    ReliableCommandClient client(&transport, ReliableCommandPolicy{2, 0});

    Envelope envelope;
    const CommandSendResult result = client.sendReliable(envelope);

    QVERIFY(!result.ok);
    QCOMPARE(result.message, QString("still offline"));
    QCOMPARE(transport.attempts(), 2);
    QCOMPARE(static_cast<int>(client.status()), static_cast<int>(CommandLinkStatus::Offline));
}

void ReliableCommandClientTests::heartbeatCachesOnlineState() {
    ScriptedTransport transport({CommandSendResult{true, "pong"}});
    ReliableCommandClient client(&transport, ReliableCommandPolicy{1, 0});

    const CommandSendResult result = client.ping("task-001");

    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("pong"));
    QCOMPARE(static_cast<int>(client.status()), static_cast<int>(CommandLinkStatus::Connected));
}

QTEST_MAIN(ReliableCommandClientTests)
#include "test_reliable_command_client.moc"
