#include <QtTest/QtTest>

#include "messages.pb.h"
#include "zmq_command_client.h"

class ZmqCommandClientTests : public QObject {
    Q_OBJECT

private slots:
    void parsesAckPayload();
    void rejectsNonAckPayload();
    void parsesFailureAckPayload();
};

void ZmqCommandClientTests::parsesAckPayload() {
    Envelope envelope;
    envelope.set_sequence(1);
    auto *ack = envelope.mutable_ack();
    ack->set_success(true);
    ack->set_message("ok");

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = ZmqCommandClient::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("ok"));
}

void ZmqCommandClientTests::rejectsNonAckPayload() {
    Envelope envelope;
    envelope.set_sequence(1);
    auto *payload = envelope.mutable_mission_load();
    payload->set_case_id("demo");

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = ZmqCommandClient::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains("ack"));
}

void ZmqCommandClientTests::parsesFailureAckPayload() {
    Envelope envelope;
    envelope.set_sequence(1);
    auto *ack = envelope.mutable_ack();
    ack->set_success(false);
    ack->set_message("busy");

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = ZmqCommandClient::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(!result.ok);
    QCOMPARE(result.message, QString("busy"));
}

QTEST_MAIN(ZmqCommandClientTests)
#include "test_zmq_command_client.moc"
