#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "competition_core/mission/task_plan_store.h"
#include "competition_core/protocol/command_handler.h"
#include "competition_core/protocol/envelope_codec.h"
#include "competition_core/task/models.h"
#include "messages.pb.h"

namespace {

void compareTaskPlans(
    const competition::TaskPlan &actual,
    const competition::TaskPlan &expected) {
    QCOMPARE(actual.task_id, expected.task_id);
    QCOMPARE(actual.task_type, expected.task_type);
    QCOMPARE(actual.metadata_json, expected.metadata_json);
    QCOMPARE(actual.start_waypoint_id, expected.start_waypoint_id);
    QCOMPARE(actual.terminal_waypoint_id, expected.terminal_waypoint_id);
    QCOMPARE(actual.waypoints.size(), expected.waypoints.size());
    for (int index = 0; index < expected.waypoints.size(); ++index) {
        const auto &expected_waypoint = expected.waypoints.at(index);
        const auto &actual_waypoint = actual.waypoints.at(index);
        QCOMPARE(actual_waypoint.id, expected_waypoint.id);
        QCOMPARE(actual_waypoint.sequence_index, expected_waypoint.sequence_index);
        QCOMPARE(actual_waypoint.action, expected_waypoint.action);
        QCOMPARE(actual_waypoint.x, expected_waypoint.x);
        QCOMPARE(actual_waypoint.y, expected_waypoint.y);
        QCOMPARE(actual_waypoint.z, expected_waypoint.z);
        QCOMPARE(actual_waypoint.payload_json, expected_waypoint.payload_json);
    }
}

competition::TaskPlan makeTaskPlan() {
    competition::TaskPlan plan;
    plan.task_id = "task-demo";
    plan.task_type = "h_problem";
    plan.start_waypoint_id = "A9B1";
    plan.terminal_waypoint_id = "touchdown";
    plan.metadata_json = R"({"case_id":"demo","execution_contract":"h_field_m_v1","landing_enabled":true})";

    competition::TaskWaypoint first;
    first.id = "A9B1";
    first.sequence_index = 0;
    first.x = 0.0;
    first.y = 0.0;
    first.z = 1.2;
    first.action = "takeoff";
    first.payload_json = R"({"cell":"A9B1"})";
    plan.waypoints.append(first);

    competition::TaskWaypoint second;
    second.id = "A8B1";
    second.sequence_index = 1;
    second.x = 0.0;
    second.y = 0.5;
    second.z = 1.2;
    second.action = "navigate";
    second.payload_json = R"({"cell":"A8B1"})";
    plan.waypoints.append(second);

    competition::TaskWaypoint touchdown;
    touchdown.id = "touchdown";
    touchdown.sequence_index = 2;
    touchdown.x = -0.75;
    touchdown.y = -0.25;
    touchdown.z = 0.0;
    touchdown.action = "land";
    touchdown.payload_json = R"({"touchdown":true})";
    plan.waypoints.append(touchdown);

    return plan;
}

competition::TaskPlan makeGenericTaskPlan() {
    competition::TaskPlan plan = makeTaskPlan();
    plan.task_type = "generic_task";
    return plan;
}

}

class TaskProtocolTests : public QObject {
    Q_OBJECT

private slots:
    void buildsAndParsesTaskPlanEnvelope();
    void storesGenericMissionLoad();
    void rejectsHProblemMissionLoadWithoutMutatingState();
    void commandStateMachineRejectsStaleStartWithoutMutatingState();
    void commandStateMachineAcceptsStopAfterStart();
    void startWithoutLoadedMissionIsRejected();
    void controlForAnotherTaskIsRejected();
    void missionReplacementResetsRunningStopAndVision();
    void completingMissionClearsLifecycleState();
    void pingDoesNotRequireMatchingTaskId();
    void commandSemanticsClassifiesAcceptedStaleStartAck();
    void commandSemanticsRejectsWrongStateStaleStartAck();
    void buildsMissionLoadWithExplicitSequence();
    void rejectsStaleMissionLoadSequence();
    void buildsAckWithRuntimeState();
    void buildsAckWithVisionTargetingState();
    void rejectsLegacyConfigJson();
};

void TaskProtocolTests::buildsAndParsesTaskPlanEnvelope() {
    const competition::TaskPlan original = makeTaskPlan();
    const Envelope envelope = competition::buildTaskPlanEnvelope(42, original);

    QCOMPARE(envelope.sequence(), 42);
    QCOMPARE(envelope.payload_case(), Envelope::kTaskPlan);
    QCOMPARE(QString::fromStdString(envelope.task_plan().task_id()), QString("task-demo"));
    QCOMPARE(envelope.task_plan().waypoints_size(), 3);
    QCOMPARE(QString::fromStdString(envelope.task_plan().waypoints(1).id()), QString("A8B1"));
    QCOMPARE(QString::fromStdString(envelope.task_plan().waypoints(2).id()), QString("touchdown"));

    QString error;
    const auto parsed = competition::taskPlanFromMessage(envelope.task_plan(), &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->metadata_json, original.metadata_json);
    QCOMPARE(parsed->terminal_waypoint_id, QString("touchdown"));
    compareTaskPlans(parsed.value(), original);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output_path = QDir(dir.path()).filePath("task_plan.json");
    QVERIFY2(competition::storeTaskPlan(original, output_path, &error), qPrintable(error));
    const auto loaded = competition::loadTaskPlan(output_path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    compareTaskPlans(loaded.value(), original);
}

void TaskProtocolTests::storesGenericMissionLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    competition::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("runtime/task_plan.json");
    const competition::AckResult ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(makeGenericTaskPlan()),
        output_path,
        &state);

    QVERIFY2(ack.success, qPrintable(ack.message));
    QCOMPARE(ack.message, QString("task plan stored"));
    QVERIFY(state.isMissionLoaded());

    QString error;
    const auto loaded = competition::loadTaskPlan(output_path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->task_id, QString("task-demo"));
    QCOMPARE(loaded->waypoints.size(), 3);

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("message_type").toString(), QString("task_plan"));
    QCOMPARE(object.value("task_type").toString(), QString("generic_task"));
}

void TaskProtocolTests::rejectsHProblemMissionLoadWithoutMutatingState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    competition::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("runtime/task_plan.json");
    const competition::AckResult ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(9, makeTaskPlan()),
        output_path,
        &state);

    QVERIFY(!ack.success);
    QVERIFY(ack.message.contains("h_problem"));
    QVERIFY(!state.isMissionLoaded());
    QCOMPARE(state.lastAcceptedSequence(), 0ULL);
    QVERIFY(!QFile::exists(output_path));
}

void TaskProtocolTests::commandStateMachineRejectsStaleStartWithoutMutatingState() {
    competition::CommandState state;
    state.setActiveTaskPlan(makeTaskPlan());
    state.setMissionLoaded(true);
    state.acceptSequence(12);

    Envelope stale_start;
    stale_start.set_sequence(12);
    stale_start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    stale_start.mutable_control_command()->set_task_id("task-demo");
    competition::MissionCommandStateMachine state_machine(&state);
    const competition::AckResult ack = state_machine.apply(stale_start);

    QVERIFY(!ack.success);
    QCOMPARE(ack.message, QString("stale command"));
    QCOMPARE(ack.task_id, QString("task-demo"));
    QVERIFY(ack.mission_loaded);
    QVERIFY(!ack.mission_running);
    QCOMPARE(ack.last_accepted_sequence, 12ULL);
    QVERIFY(!state.isStartRequested());
}

void TaskProtocolTests::commandStateMachineAcceptsStopAfterStart() {
    competition::CommandState state;
    state.setActiveTaskPlan(makeTaskPlan());
    state.setMissionLoaded(true);

    Envelope start;
    start.set_sequence(13);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    start.mutable_control_command()->set_task_id("task-demo");
    competition::MissionCommandStateMachine state_machine(&state);
    const competition::AckResult start_ack = state_machine.apply(start);
    QVERIFY2(start_ack.success, qPrintable(start_ack.message));
    QVERIFY(state.isStartRequested());
    QVERIFY(!state.isStopRequested());

    Envelope stop;
    stop.set_sequence(14);
    stop.mutable_control_command()->set_type(COMMAND_TYPE_STOP_MISSION);
    stop.mutable_control_command()->set_task_id("task-demo");
    const competition::AckResult stop_ack = state_machine.apply(stop);
    QVERIFY2(stop_ack.success, qPrintable(stop_ack.message));
    QVERIFY(state.isStartRequested());
    QVERIFY(state.isStopRequested());
    QCOMPARE(state.lastAcceptedSequence(), 14ULL);
}

void TaskProtocolTests::startWithoutLoadedMissionIsRejected() {
    competition::CommandState state;
    state.setActiveTaskPlan(makeTaskPlan());
    state.acceptSequence(4);

    Envelope start;
    start.set_sequence(5);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    start.mutable_control_command()->set_task_id("task-demo");
    competition::MissionCommandStateMachine state_machine(&state);
    const competition::AckResult ack = state_machine.apply(start);

    QVERIFY(!ack.success);
    QVERIFY(ack.message.contains("loaded"));
    QVERIFY(!state.isStartRequested());
    QCOMPARE(state.lastAcceptedSequence(), 4ULL);
}

void TaskProtocolTests::controlForAnotherTaskIsRejected() {
    const QList<CommandType> command_types{
        COMMAND_TYPE_START_MISSION,
        COMMAND_TYPE_STOP_MISSION,
        COMMAND_TYPE_ARM_TARGETING,
        COMMAND_TYPE_RESET_TARGETING,
    };

    for (const CommandType command_type : command_types) {
        competition::CommandState state;
        state.replaceMission(makeTaskPlan());
        state.acceptSequence(10);

        Envelope control;
        control.set_sequence(11);
        control.mutable_control_command()->set_type(command_type);
        control.mutable_control_command()->set_task_id("another-task");
        competition::MissionCommandStateMachine state_machine(&state);
        const competition::AckResult ack = state_machine.apply(control);

        QVERIFY(!ack.success);
        QVERIFY(ack.message.contains("task"));
        QCOMPARE(state.lastAcceptedSequence(), 10ULL);
    }
}

void TaskProtocolTests::missionReplacementResetsRunningStopAndVision() {
    competition::CommandState state;
    state.replaceMission(makeTaskPlan());
    state.requestStart();
    state.requestStop();
    state.armVisionTargeting();

    competition::TaskPlan replacement = makeTaskPlan();
    replacement.task_id = "replacement-task";
    state.replaceMission(replacement);

    QVERIFY(state.isMissionLoaded());
    QCOMPARE(state.activeTaskPlan().task_id, QString("replacement-task"));
    QVERIFY(!state.isStartRequested());
    QVERIFY(!state.isStopRequested());
    QVERIFY(!state.isVisionTargetingArmed());
}

void TaskProtocolTests::completingMissionClearsLifecycleState() {
    competition::CommandState state;
    state.replaceMission(makeTaskPlan());
    state.requestStart();
    state.requestStop();
    state.armVisionTargeting();

    state.completeMission();

    QVERIFY(!state.isMissionLoaded());
    QVERIFY(!state.isStartRequested());
    QVERIFY(!state.isStopRequested());
    QVERIFY(!state.isVisionTargetingArmed());
}

void TaskProtocolTests::pingDoesNotRequireMatchingTaskId() {
    competition::CommandState state;
    state.replaceMission(makeTaskPlan());
    state.acceptSequence(20);

    Envelope ping;
    ping.set_sequence(21);
    ping.mutable_control_command()->set_type(COMMAND_TYPE_PING);
    ping.mutable_control_command()->set_task_id("another-task");
    competition::MissionCommandStateMachine state_machine(&state);
    const competition::AckResult ack = state_machine.apply(ping);

    QVERIFY2(ack.success, qPrintable(ack.message));
    QCOMPARE(ack.message, QString("pong"));
    QCOMPARE(state.lastAcceptedSequence(), 20ULL);
}

void TaskProtocolTests::commandSemanticsClassifiesAcceptedStaleStartAck() {
    Envelope start;
    start.set_sequence(21);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    start.mutable_control_command()->set_task_id("task-demo");

    const competition::AckResult stale_ack{
        false,
        "stale command",
        "task-demo",
        true,
        true,
        21,
    };

    QVERIFY(competition::isCommandAlreadyAccepted(start, stale_ack));
}

void TaskProtocolTests::commandSemanticsRejectsWrongStateStaleStartAck() {
    Envelope start;
    start.set_sequence(22);
    start.mutable_control_command()->set_type(COMMAND_TYPE_START_MISSION);
    start.mutable_control_command()->set_task_id("task-demo");

    const competition::AckResult stale_ack{
        false,
        "stale command",
        "task-demo",
        true,
        false,
        22,
    };

    QVERIFY(!competition::isCommandAlreadyAccepted(start, stale_ack));
}

void TaskProtocolTests::buildsMissionLoadWithExplicitSequence() {
    const Envelope envelope = competition::buildMissionLoadEnvelope(7, makeTaskPlan());

    QCOMPARE(envelope.sequence(), 7ULL);
    QCOMPARE(envelope.payload_case(), Envelope::kMissionLoad);
    QCOMPARE(QString::fromStdString(envelope.mission_load().task_id()), QString("task-demo"));
}

void TaskProtocolTests::rejectsStaleMissionLoadSequence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    competition::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("runtime/task_plan.json");
    const competition::AckResult first_ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(8, makeGenericTaskPlan()),
        output_path,
        &state);
    QVERIFY2(first_ack.success, qPrintable(first_ack.message));
    QCOMPARE(state.lastAcceptedSequence(), 8ULL);

    const competition::AckResult stale_ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(8, makeGenericTaskPlan()),
        output_path,
        &state);
    QVERIFY(!stale_ack.success);
    QCOMPARE(stale_ack.message, QString("stale command"));
    QCOMPARE(state.lastAcceptedSequence(), 8ULL);
}

void TaskProtocolTests::buildsAckWithRuntimeState() {
    competition::CommandState state;
    state.setActiveTaskPlan(makeTaskPlan());
    state.setMissionLoaded(true);
    state.requestStart();
    state.acceptSequence(15);

    const Envelope envelope = competition::buildAckEnvelope({true, "start accepted"}, state);

    QCOMPARE(envelope.payload_case(), Envelope::kAck);
    QCOMPARE(envelope.ack().success(), true);
    QCOMPARE(QString::fromStdString(envelope.ack().message()), QString("start accepted"));
    QCOMPARE(QString::fromStdString(envelope.ack().task_id()), QString("task-demo"));
    QCOMPARE(envelope.ack().mission_loaded(), true);
    QCOMPARE(envelope.ack().mission_running(), true);
    QCOMPARE(envelope.ack().last_accepted_sequence(), 15ULL);
}

void TaskProtocolTests::buildsAckWithVisionTargetingState() {
    competition::CommandState state;
    state.armVisionTargeting();

    const Envelope envelope = competition::buildAckEnvelope({true, "vision armed"}, state);

    QCOMPARE(envelope.payload_case(), Envelope::kAck);
    QCOMPARE(envelope.ack().vision_armed(), true);
}

void TaskProtocolTests::rejectsLegacyConfigJson() {
    QJsonObject legacy;
    legacy["message_type"] = "config";
    legacy["task_id"] = "legacy";
    legacy["waypoints"] = QJsonArray{};

    QString error;
    const auto parsed = competition::taskPlanFromJsonObject(legacy, &error);

    QVERIFY(!parsed.has_value());
    QVERIFY(error.contains("message_type"));
}

QTEST_MAIN(TaskProtocolTests)
#include "test_task_protocol.moc"
