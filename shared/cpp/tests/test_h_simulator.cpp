#include <QtTest/QtTest>

#include "h_problem_core/runtime/simulator.h"

namespace {

hcore::CaseConfig makeCase() {
    hcore::CaseConfig config;
    config.case_id = "plan-test";
    config.start_cell = "A1B1";
    config.tick_interval_ms = 10;
    hcore::LandingProfile landing;
    landing.takeoff_anchor_cm = {450.0, 350.0};
    landing.cruise_height_cm = 120.0;
    landing.descent_angle_deg = 45.0;
    landing.touchdown_radius_cm = 18.0;
    config.landing = landing;
    config.animals.append({"A2B1", "elephant", 1});
    config.animals.append({"A3B1", "monkey", 2});
    return config;
}

}

class HSimulatorTests : public QObject {
    Q_OBJECT

private slots:
    void emitsGenericTaskEventStream();
    void prefersProvidedTaskPlan();
};

void HSimulatorTests::emitsGenericTaskEventStream() {
    QString error;
    const auto stream = hcore::simulateTaskStream(makeCase(), {}, &error);
    QVERIFY2(stream.has_value(), qPrintable(error));

    QCOMPARE(stream->plan.task_id, QString("plan-test"));
    QVERIFY(!stream->events.isEmpty());

    int detection_count = 0;
    for (const competition::TaskEvent &event : stream->events) {
        if (event.event_type == "detection") {
            ++detection_count;
            QVERIFY(event.payload_json.contains("animal_name"));
        }
    }

    QCOMPARE(detection_count, 2);
    QVERIFY(stream->summary.success);
    QCOMPARE(stream->summary.task_id, QString("plan-test"));
    QVERIFY(stream->summary.payload_json.contains("totals"));
    QVERIFY(stream->summary.payload_json.contains("elephant"));
    QVERIFY(stream->summary.payload_json.contains("monkey"));
}

void HSimulatorTests::prefersProvidedTaskPlan() {
    competition::TaskPlan plan;
    plan.task_id = "plan-test";
    plan.task_type = "h_problem";
    plan.start_waypoint_id = "A1B1";
    plan.terminal_waypoint_id = "A3B1";
    plan.waypoints = {{"A1B1", 0}, {"A2B1", 1}, {"A3B1", 2}};

    QString error;
    const auto stream = hcore::simulateTaskStream(makeCase(), plan, &error);
    QVERIFY2(stream.has_value(), qPrintable(error));
    QCOMPARE(stream->plan.task_id, QString("plan-test"));
    QCOMPARE(stream->plan.waypoints.size(), plan.waypoints.size());

    QStringList telemetry_cells;
    for (const competition::TaskEvent &event : stream->events) {
        if (event.event_type == "telemetry") {
            telemetry_cells.append(event.waypoint_id);
        }
    }
    QCOMPARE(telemetry_cells, QStringList({"A1B1", "A2B1", "A3B1"}));
}

QTEST_MAIN(HSimulatorTests)
#include "test_h_simulator.moc"
