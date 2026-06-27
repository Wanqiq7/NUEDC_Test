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

competition::TaskPlan makeTaskPlan() {
    competition::TaskPlan plan;
    plan.task_id = "task-demo";
    plan.task_type = "h_problem";
    plan.start_waypoint_id = "A9B1";
    plan.terminal_waypoint_id = "A8B1";
    plan.metadata_json = R"({"case_id":"demo","landing_enabled":true})";

    competition::TaskWaypoint first;
    first.id = "A9B1";
    first.sequence_index = 0;
    first.payload_json = R"({"cell":"A9B1"})";
    plan.waypoints.append(first);

    competition::TaskWaypoint second;
    second.id = "A8B1";
    second.sequence_index = 1;
    second.payload_json = R"({"cell":"A8B1"})";
    plan.waypoints.append(second);

    return plan;
}

}

class TaskProtocolTests : public QObject {
    Q_OBJECT

private slots:
    void buildsAndParsesTaskPlanEnvelope();
    void storesGenericMissionLoad();
    void buildsMissionLoadWithExplicitSequence();
    void rejectsStaleMissionLoadSequence();
    void buildsAckWithRuntimeState();
};

void TaskProtocolTests::buildsAndParsesTaskPlanEnvelope() {
    const Envelope envelope = competition::buildTaskPlanEnvelope(42, makeTaskPlan());

    QCOMPARE(envelope.sequence(), 42);
    QCOMPARE(envelope.payload_case(), Envelope::kTaskPlan);
    QCOMPARE(QString::fromStdString(envelope.task_plan().task_id()), QString("task-demo"));
    QCOMPARE(envelope.task_plan().waypoints_size(), 2);
    QCOMPARE(QString::fromStdString(envelope.task_plan().waypoints(1).id()), QString("A8B1"));

    QString error;
    const auto parsed = competition::taskPlanFromMessage(envelope.task_plan(), &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->task_id, QString("task-demo"));
    QCOMPARE(parsed->task_type, QString("h_problem"));
    QCOMPARE(parsed->waypoints.size(), 2);
    QCOMPARE(parsed->waypoints.at(0).payload_json, QString(R"({"cell":"A9B1"})"));
}

void TaskProtocolTests::storesGenericMissionLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    competition::CommandState state;
    const QString output_path = QDir(dir.path()).filePath("runtime/task_plan.json");
    const competition::AckResult ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(makeTaskPlan()),
        output_path,
        &state);

    QVERIFY2(ack.success, qPrintable(ack.message));
    QCOMPARE(ack.message, QString("task plan stored"));
    QVERIFY(state.isMissionLoaded());

    QString error;
    const auto loaded = competition::loadTaskPlan(output_path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->task_id, QString("task-demo"));
    QCOMPARE(loaded->waypoints.size(), 2);

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("message_type").toString(), QString("task_plan"));
    QCOMPARE(object.value("task_type").toString(), QString("h_problem"));
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
        competition::buildMissionLoadEnvelope(8, makeTaskPlan()),
        output_path,
        &state);
    QVERIFY2(first_ack.success, qPrintable(first_ack.message));
    QCOMPARE(state.lastAcceptedSequence(), 8ULL);

    const competition::AckResult stale_ack = competition::handleEnvelopeCommand(
        competition::buildMissionLoadEnvelope(8, makeTaskPlan()),
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

QTEST_MAIN(TaskProtocolTests)
#include "test_task_protocol.moc"
