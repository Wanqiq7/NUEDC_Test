#include <QtTest/QtTest>

#include "messages.pb.h"
#include "h_problem/mission/h_mission_load_adapter.h"

class MissionLoadAdapterTests : public QObject {
    Q_OBJECT

private slots:
    void buildsMissionLoadEnvelopeFromPlan();
    void defaultsMissingOptionalLandingFieldsToZero();
};

void MissionLoadAdapterTests::buildsMissionLoadEnvelopeFromPlan() {
    MissionPlanData plan;
    plan.case_id = "case-123";
    plan.start_cell = "A1B1";
    plan.no_fly_cells = {"A2B2", "A3B2"};
    plan.route = {"A1B1", "A1B2", "A1B3"};
    plan.terminal_cell = "A1B3";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 120.0;
    plan.takeoff_anchor_y_cm = 130.0;

    const Envelope envelope = MissionLoadAdapter::buildMissionLoadEnvelope(plan);
    QCOMPARE(envelope.sequence(), 0);
    QCOMPARE(envelope.payload_case(), Envelope::kMissionLoad);
    const auto &payload = envelope.mission_load();
    QCOMPARE(QString::fromStdString(payload.task_id()), plan.case_id);
    QCOMPARE(QString::fromStdString(payload.task_type()), QString("h_problem"));
    QCOMPARE(QString::fromStdString(payload.start_waypoint_id()), plan.start_cell);
    QCOMPARE(QString::fromStdString(payload.terminal_waypoint_id()), plan.terminal_cell);
    QCOMPARE(payload.waypoints_size(), 3);
    QCOMPARE(QString::fromStdString(payload.waypoints(2).id()), QString("A1B3"));
    QVERIFY(QString::fromStdString(payload.metadata_json()).contains("\"landing_enabled\":true"));
    QVERIFY(QString::fromStdString(payload.metadata_json()).contains("\"descent_angle_deg\":45"));
    QVERIFY(QString::fromStdString(payload.metadata_json()).contains("\"takeoff_anchor_x_cm\":120"));
    QVERIFY(QString::fromStdString(payload.metadata_json()).contains("\"takeoff_anchor_y_cm\":130"));
}

void MissionLoadAdapterTests::defaultsMissingOptionalLandingFieldsToZero() {
    MissionPlanData plan;
    plan.case_id = "case-123";
    plan.start_cell = "A1B1";
    plan.route = {"A1B1"};
    plan.terminal_cell = "A1B1";

    const Envelope envelope = MissionLoadAdapter::buildMissionLoadEnvelope(plan);
    const auto &payload = envelope.mission_load();
    const QString metadata = QString::fromStdString(payload.metadata_json());
    QVERIFY(metadata.contains("\"descent_angle_deg\":null"));
    QVERIFY(metadata.contains("\"takeoff_anchor_x_cm\":null"));
    QVERIFY(metadata.contains("\"takeoff_anchor_y_cm\":null"));
}

QTEST_MAIN(MissionLoadAdapterTests)
#include "test_mission_load_adapter.moc"
