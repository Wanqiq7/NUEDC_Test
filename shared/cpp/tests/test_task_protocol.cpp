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
    QVERIFY(state.mission_loaded);

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

QTEST_MAIN(TaskProtocolTests)
#include "test_task_protocol.moc"
