#include <QtTest/QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "h_problem_core/protocol/command_handler.h"
#include "h_problem_core/protocol/envelope_builder.h"
#include "messages.pb.h"

namespace {

Envelope makeMissionLoad() {
    hcore::MissionPlan plan;
    plan.case_id = "demo";
    plan.start_cell = "A9B1";
    plan.no_fly_cells = {"A1B2", "A2B2"};
    plan.route = {"A9B1", "A8B1"};
    plan.terminal_cell = "A8B1";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 450.0;
    plan.takeoff_anchor_y_cm = 350.0;
    return hcore::buildMissionLoadEnvelope(plan);
}

}

class HCommandHandlerTests : public QObject {
    Q_OBJECT

private slots:
    void storesMissionLoadAndMarksLoaded();
    void handlesStartStopPing();
    void rejectsUnsupportedPayload();
    void rejectsInvalidProtobufBytes();
};

void HCommandHandlerTests::storesMissionLoadAndMarksLoaded() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    hcore::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("active_mission_plan.json");
    const hcore::AckResult ack = hcore::handleEnvelopeCommand(makeMissionLoad(), output_path, &state);

    QVERIFY(ack.success);
    QCOMPARE(ack.message, QString("task plan stored"));
    QVERIFY(state.mission_loaded);
    QCOMPARE(state.active_task_plan.task_id, QString("demo"));
    QVERIFY(QFile::exists(output_path));
}

void HCommandHandlerTests::handlesStartStopPing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    hcore::CommandState state;

    Envelope start;
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    const hcore::AckResult start_ack = hcore::handleEnvelopeCommand(start, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(start_ack.success);
    QCOMPARE(start_ack.message, QString("start accepted"));
    QVERIFY(state.start_requested);
    QVERIFY(!state.stop_requested);

    Envelope ping;
    ping.mutable_control_command()->set_type(COMMAND_TYPE_PING);
    const hcore::AckResult ping_ack = hcore::handleEnvelopeCommand(ping, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(ping_ack.success);
    QCOMPARE(ping_ack.message, QString("pong"));

    Envelope stop;
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);
    const hcore::AckResult stop_ack = hcore::handleEnvelopeCommand(stop, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(stop_ack.success);
    QCOMPARE(stop_ack.message, QString("stop accepted"));
    QVERIFY(state.stop_requested);
}

void HCommandHandlerTests::rejectsUnsupportedPayload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Envelope envelope;
    envelope.mutable_task_event()->set_event_type("telemetry");
    hcore::CommandState state;
    const hcore::AckResult ack = hcore::handleEnvelopeCommand(envelope, QDir(dir.path()).filePath("plan.json"), &state);

    QVERIFY(!ack.success);
    QCOMPARE(ack.message, QString("unsupported payload"));
}

void HCommandHandlerTests::rejectsInvalidProtobufBytes() {
    hcore::CommandState state;
    const hcore::AckResult ack = hcore::handleCommandBytes(QByteArray("not protobuf"), "unused.json", &state);

    QVERIFY(!ack.success);
    QCOMPARE(ack.message, QString("invalid protobuf"));
}

QTEST_MAIN(HCommandHandlerTests)
#include "test_h_command_handler.moc"
