#include <QtTest/QtTest>

#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>
#include <atomic>

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
    void rejectsStaleControlCommandSequence();
    void rejectsLowerSequenceAfterHigherConcurrentCommand();
    void commandStateIsSafeForConcurrentCommandHandling();
    void handlesVisionTargetingCommandsWithMonotonicSequences();
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
    QVERIFY(state.isMissionLoaded());
    QCOMPARE(state.activeTaskPlan().task_id, QString("demo"));
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
    QVERIFY(state.isStartRequested());
    QVERIFY(!state.isStopRequested());

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
    QVERIFY(state.isStopRequested());
}

void HCommandHandlerTests::rejectsStaleControlCommandSequence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    hcore::CommandState state;

    Envelope start;
    start.set_sequence(21);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    const hcore::AckResult start_ack = hcore::handleEnvelopeCommand(start, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(start_ack.success);
    QCOMPARE(state.lastAcceptedSequence(), 21ULL);

    Envelope stop;
    stop.set_sequence(20);
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);
    const hcore::AckResult stale_ack = hcore::handleEnvelopeCommand(stop, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(!stale_ack.success);
    QCOMPARE(stale_ack.message, QString("stale command"));
    QVERIFY(!state.isStopRequested());
    QCOMPARE(state.lastAcceptedSequence(), 21ULL);
}

void HCommandHandlerTests::rejectsLowerSequenceAfterHigherConcurrentCommand() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    hcore::CommandState state;

    Envelope start;
    start.set_sequence(30);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    const hcore::AckResult start_ack = hcore::handleEnvelopeCommand(start, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(start_ack.success);
    QVERIFY(state.isStartRequested());
    QCOMPARE(state.lastAcceptedSequence(), 30ULL);

    Envelope stop;
    stop.set_sequence(29);
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);
    const hcore::AckResult stale_ack = hcore::handleEnvelopeCommand(stop, QDir(dir.path()).filePath("plan.json"), &state);
    QVERIFY(!stale_ack.success);
    QCOMPARE(stale_ack.message, QString("stale command"));
    QVERIFY(!state.isStopRequested());
    QCOMPARE(state.lastAcceptedSequence(), 30ULL);
}
void HCommandHandlerTests::commandStateIsSafeForConcurrentCommandHandling() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    hcore::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("plan.json");
    Envelope start;
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    Envelope stop;
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);

    std::atomic_bool failed{false};
    QVector<QThread *> threads;
    for (int thread_index = 0; thread_index < 4; ++thread_index) {
        QThread *thread = QThread::create([&, thread_index]() {
            for (int iteration = 0; iteration < 200; ++iteration) {
                const Envelope &command = ((iteration + thread_index) % 2 == 0) ? start : stop;
                const hcore::AckResult ack = hcore::handleEnvelopeCommand(command, output_path, &state);
                if (!ack.success) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                static_cast<void>(state.isStartRequested());
                static_cast<void>(state.isStopRequested());
            }
        });
        threads.append(thread);
        thread->start();
    }

    for (QThread *thread : threads) {
        QVERIFY(thread->wait(5000));
        delete thread;
    }
    QVERIFY(!failed.load(std::memory_order_relaxed));
}

void HCommandHandlerTests::handlesVisionTargetingCommandsWithMonotonicSequences() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    hcore::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("plan.json");

    Envelope arm;
    arm.set_sequence(41);
    arm.mutable_control_command()->set_type(COMMAND_TYPE_ARM_TARGETING);
    const hcore::AckResult arm_ack = hcore::handleEnvelopeCommand(arm, output_path, &state);
    QVERIFY(arm_ack.success);
    QVERIFY(arm_ack.vision_armed);
    QVERIFY(state.isVisionTargetingArmed());

    Envelope stale_reset;
    stale_reset.set_sequence(40);
    stale_reset.mutable_control_command()->set_type(COMMAND_TYPE_RESET_TARGETING);
    const hcore::AckResult stale_ack = hcore::handleEnvelopeCommand(stale_reset, output_path, &state);
    QVERIFY(!stale_ack.success);
    QCOMPARE(stale_ack.message, QString("stale command"));
    QVERIFY(stale_ack.vision_armed);
    QVERIFY(state.isVisionTargetingArmed());
    QCOMPARE(state.lastAcceptedSequence(), 41ULL);

    Envelope reset;
    reset.set_sequence(42);
    reset.mutable_control_command()->set_type(COMMAND_TYPE_RESET_TARGETING);
    const hcore::AckResult reset_ack = hcore::handleEnvelopeCommand(reset, output_path, &state);
    QVERIFY(reset_ack.success);
    QVERIFY(!reset_ack.vision_armed);
    QVERIFY(!state.isVisionTargetingArmed());

    Envelope stale_arm;
    stale_arm.set_sequence(41);
    stale_arm.mutable_control_command()->set_type(COMMAND_TYPE_ARM_TARGETING);
    const hcore::AckResult stale_arm_ack = hcore::handleEnvelopeCommand(stale_arm, output_path, &state);
    QVERIFY(!stale_arm_ack.success);
    QCOMPARE(stale_arm_ack.message, QString("stale command"));
    QVERIFY(!stale_arm_ack.vision_armed);
    QVERIFY(!state.isVisionTargetingArmed());
    QCOMPARE(state.lastAcceptedSequence(), 42ULL);

    Envelope rearm;
    rearm.set_sequence(43);
    rearm.mutable_control_command()->set_type(COMMAND_TYPE_ARM_TARGETING);
    QVERIFY(hcore::handleEnvelopeCommand(rearm, output_path, &state).success);
    QVERIFY(state.isVisionTargetingArmed());

    Envelope stop;
    stop.set_sequence(44);
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);
    const hcore::AckResult stop_ack = hcore::handleEnvelopeCommand(stop, output_path, &state);
    QVERIFY(stop_ack.success);
    QVERIFY(!stop_ack.vision_armed);
    QVERIFY(!state.isVisionTargetingArmed());
    QVERIFY(!hcore::buildAckEnvelope(stop_ack, state).ack().vision_armed());
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
