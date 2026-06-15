#include <QtTest/QtTest>

#include "framework/communication/envelope_codec.h"
#include "messages.pb.h"
#include "framework/communication/zmq_command_client.h"

class EnvelopeCodecTests : public QObject {
    Q_OBJECT

private slots:
    void parsesAckPayload();
    void rejectsNonAckPayload();
    void keepsZmqCommandClientWrapperCompatible();
};

void EnvelopeCodecTests::parsesAckPayload() {
    Envelope envelope;
    envelope.set_sequence(1);
    auto *ack = envelope.mutable_ack();
    ack->set_success(true);
    ack->set_message("ok");

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = EnvelopeCodec::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("ok"));
}

void EnvelopeCodecTests::rejectsNonAckPayload() {
    Envelope envelope;
    envelope.set_sequence(1);
    envelope.mutable_mission_load()->set_task_id("demo");

    std::string bytes;
    envelope.SerializeToString(&bytes);

    const auto result = EnvelopeCodec::parseAck(QByteArray::fromStdString(bytes));
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains("ack"));
}

void EnvelopeCodecTests::keepsZmqCommandClientWrapperCompatible() {
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

QTEST_MAIN(EnvelopeCodecTests)
#include "test_envelope_codec.moc"
