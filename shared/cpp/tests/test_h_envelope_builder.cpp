#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "h_problem_core/protocol/envelope_builder.h"
#include "messages.pb.h"

class HEnvelopeBuilderTests : public QObject {
    Q_OBJECT

private slots:
    void buildsGridConfigEnvelope();
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
