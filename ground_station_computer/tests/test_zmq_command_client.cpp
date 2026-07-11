#include <QtTest/QtTest>

#include "messages.pb.h"
#include "framework/communication/zmq_command_client.h"

class ZmqCommandClientTests : public QObject {
    Q_OBJECT

private slots:
    void parsesAckPayload();
    void parsesAckRuntimeState();
    void rejectsNonAckPayload();
    void parsesFailureAckPayload();
    void buildsControlCommandWithTaskId();
    void buildsArmTargetingControlCommand();
    void buildsResetTargetingControlCommand();
    void buildsControlCommandWithMonotonicSequence();
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

void ZmqCommandClientTests::parsesAckRuntimeState() {
    Envelope envelope;
    envelope.set_sequence(9);
    auto *ack = envelope.mutable_ack();
    ack->set_success(true);
    ack->set_message("start accepted");
    ack->set_task_id("task-123");
    ack->set_mission_loaded(true);
    ack->set_mission_running(true);
    ack->set_last_accepted_sequence(9);

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = ZmqCommandClient::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("start accepted"));
    QCOMPARE(result.task_id, QString("task-123"));
    QVERIFY(result.mission_loaded);
    QVERIFY(result.mission_running);
    QCOMPARE(result.last_accepted_sequence, 9ULL);
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

void ZmqCommandClientTests::buildsArmTargetingControlCommand() {
    const Envelope envelope = ZmqCommandClient::buildControlCommandEnvelope(
        101,
        GroundControlCommandType::ArmTargeting,
        "task-123");

    QCOMPARE(envelope.payload_case(), Envelope::kControlCommand);
    QCOMPARE(envelope.control_command().type(), CommandType::COMMAND_TYPE_ARM_TARGETING);
    QCOMPARE(QString::fromStdString(envelope.control_command().task_id()), QString("task-123"));
}

void ZmqCommandClientTests::buildsResetTargetingControlCommand() {
    const Envelope envelope = ZmqCommandClient::buildControlCommandEnvelope(
        102,
        GroundControlCommandType::ResetTargeting,
        "task-123");

    QCOMPARE(envelope.payload_case(), Envelope::kControlCommand);
    QCOMPARE(envelope.control_command().type(), CommandType::COMMAND_TYPE_RESET_TARGETING);
    QCOMPARE(QString::fromStdString(envelope.control_command().task_id()), QString("task-123"));
}

void ZmqCommandClientTests::buildsControlCommandWithMonotonicSequence() {
    const Envelope first = ZmqCommandClient::buildControlCommandEnvelope(GroundControlCommandType::Ping);
    const Envelope second = ZmqCommandClient::buildControlCommandEnvelope(GroundControlCommandType::Ping);

    QVERIFY(first.sequence() > 0);
    QVERIFY(first.sequence() > (1ULL << 20));
    QVERIFY(second.sequence() > first.sequence());
}

QTEST_MAIN(ZmqCommandClientTests)
#include "test_zmq_command_client.moc"
