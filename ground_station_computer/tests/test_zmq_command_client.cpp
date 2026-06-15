#include <QtTest/QtTest>

#include "messages.pb.h"
#include "framework/communication/zmq_command_client.h"

class ZmqCommandClientTests : public QObject {
    Q_OBJECT

private slots:
    void parsesAckPayload();
    void rejectsNonAckPayload();
    void parsesFailureAckPayload();
    void buildsControlCommandWithTaskId();
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
    payload->set_task_id("demo");

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

void ZmqCommandClientTests::buildsControlCommandWithTaskId() {
    const Envelope envelope = ZmqCommandClient::buildControlCommandEnvelope(
        GroundControlCommandType::StartMission,
        "task-123");

    QCOMPARE(envelope.payload_case(), Envelope::kControlCommand);
    QCOMPARE(envelope.control_command().type(), CommandType::COMMAND_TYPE_START_MISSION);
    QCOMPARE(QString::fromStdString(envelope.control_command().task_id()), QString("task-123"));
}

QTEST_MAIN(ZmqCommandClientTests)
#include "test_zmq_command_client.moc"
