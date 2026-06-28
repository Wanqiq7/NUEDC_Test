#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "h_problem_core/protocol/envelope_builder.h"
#include "messages.pb.h"

class HEnvelopeBuilderTests : public QObject {
    Q_OBJECT

private slots:
    void buildsGridConfigEnvelope();
    void roundTripsMissionPlanThroughCanonicalTaskPlan();
    void buildsRuntimeEnvelopes();
    void buildsAckEnvelope();
};

void HEnvelopeBuilderTests::buildsGridConfigEnvelope() {
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

    const Envelope envelope = hcore::buildGridConfigEnvelope(7, plan);

    QCOMPARE(envelope.sequence(), 7);
    QCOMPARE(envelope.payload_case(), Envelope::kTaskPlan);
    QCOMPARE(QString::fromStdString(envelope.task_plan().task_id()), QString("demo"));
    QCOMPARE(envelope.task_plan().waypoints_size(), 2);
    QCOMPARE(QString::fromStdString(envelope.task_plan().task_type()), QString("h_problem"));

    QString error;
    const auto round_trip = hcore::missionPlanFromGridConfig(envelope.task_plan(), &error);
    QVERIFY2(round_trip.has_value(), qPrintable(error));
    QCOMPARE(round_trip->route, plan.route);
    QCOMPARE(round_trip->landing_enabled, true);
}

void HEnvelopeBuilderTests::roundTripsMissionPlanThroughCanonicalTaskPlan() {
    hcore::MissionPlan plan;
    plan.case_id = "demo";
    plan.start_cell = "A9B1";
    plan.no_fly_cells = {"A1B2", "A2B2", "A3B2"};
    plan.route = {"A9B1", "A8B1", "A7B1"};
    plan.terminal_cell = "A7B1";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 450.0;
    plan.takeoff_anchor_y_cm = 350.0;

    const competition::TaskPlan task_plan = hcore::taskPlanFromMissionPlan(plan);
    QCOMPARE(task_plan.task_id, QString("demo"));
    QCOMPARE(task_plan.task_type, QString("h_problem"));
    QCOMPARE(task_plan.start_waypoint_id, QString("A9B1"));
    QCOMPARE(task_plan.terminal_waypoint_id, QString("A7B1"));
    QCOMPARE(task_plan.waypoints.size(), 3);
    QCOMPARE(task_plan.waypoints.at(1).id, QString("A8B1"));

    const QJsonObject metadata = QJsonDocument::fromJson(task_plan.metadata_json.toUtf8()).object();
    QCOMPARE(metadata.value("terminal_cell").toString(), QString("A7B1"));
    QCOMPARE(metadata.value("no_fly_cells").toArray().at(2).toString(), QString("A3B2"));
    QCOMPARE(metadata.value("landing_enabled").toBool(), true);

    QString error;
    const auto decoded = hcore::missionPlanFromTaskPlan(task_plan, &error);
    QVERIFY2(decoded.has_value(), qPrintable(error));
    QCOMPARE(decoded->case_id, plan.case_id);
    QCOMPARE(decoded->no_fly_cells, plan.no_fly_cells);
    QCOMPARE(decoded->route, plan.route);
    QCOMPARE(decoded->terminal_cell, plan.terminal_cell);
    QCOMPARE(decoded->landing_enabled, true);
    QCOMPARE(decoded->descent_angle_deg.value_or(0.0), 45.0);
    QCOMPARE(decoded->takeoff_anchor_x_cm.value_or(0.0), 450.0);
    QCOMPARE(decoded->takeoff_anchor_y_cm.value_or(0.0), 350.0);
}

void HEnvelopeBuilderTests::buildsRuntimeEnvelopes() {
    const Envelope telemetry = hcore::buildTelemetryEnvelope(1, "A1B1", 2, 3);
    const Envelope detection = hcore::buildDetectionEnvelope(2, "A2B2", "elephant", 4);
    QMap<QString, quint32> totals;
    totals.insert("monkey", 2);
    totals.insert("elephant", 1);
    const Envelope summary = hcore::buildSummaryEnvelope(3, totals, 9);

    QCOMPARE(telemetry.payload_case(), Envelope::kTaskEvent);
    QCOMPARE(QString::fromStdString(telemetry.task_event().event_type()), QString("telemetry"));
    QCOMPARE(QString::fromStdString(telemetry.task_event().waypoint_id()), QString("A1B1"));
    QCOMPARE(detection.payload_case(), Envelope::kTaskEvent);
    QCOMPARE(QString::fromStdString(detection.task_event().event_type()), QString("detection"));
    QVERIFY(QString::fromStdString(detection.task_event().payload_json()).contains("elephant"));
    QCOMPARE(summary.payload_case(), Envelope::kTaskSummary);
    QCOMPARE(summary.task_summary().visited_waypoints(), 9u);
    const QJsonObject summary_payload = QJsonDocument::fromJson(
        QByteArray::fromStdString(summary.task_summary().payload_json())).object();
    QCOMPARE(summary_payload.value("totals").toObject().value("elephant").toInt(), 1);
    QCOMPARE(summary_payload.value("totals").toObject().value("monkey").toInt(), 2);
}

void HEnvelopeBuilderTests::buildsAckEnvelope() {
    const Envelope envelope = hcore::buildAckEnvelope(true, "pong");
    QCOMPARE(envelope.sequence(), 0);
    QCOMPARE(envelope.payload_case(), Envelope::kAck);
    QVERIFY(envelope.ack().success());
    QCOMPARE(QString::fromStdString(envelope.ack().message()), QString("pong"));
}

QTEST_MAIN(HEnvelopeBuilderTests)
#include "test_h_envelope_builder.moc"
