#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "h_problem_core/runtime/simulator.h"

namespace {

hcore::CaseConfig makeCase() {
    hcore::CaseConfig config;
    config.case_id = "plan-test";
    config.start_cell = "A9B1";
    config.tick_interval_ms = 10;
    hcore::LandingProfile landing;
    landing.takeoff_anchor_cm = {450.0, 350.0};
    landing.cruise_height_cm = 120.0;
    landing.descent_angle_deg = 45.0;
    landing.touchdown_radius_cm = 18.0;
    config.landing = landing;
    config.animals.append({"A8B1", "elephant", 1});
    config.animals.append({"A7B1", "monkey", 2});
    return config;
}

QJsonObject telemetryPayload(const competition::TaskEvent &event) {
    return QJsonDocument::fromJson(event.payload_json.toUtf8()).object();
}

QVector<competition::TaskEvent> telemetryEvents(const QVector<competition::TaskEvent> &events) {
    QVector<competition::TaskEvent> result;
    for (const competition::TaskEvent &event : events) {
        if (event.event_type == "telemetry") {
            result.append(event);
        }
    }
    return result;
}

QStringList detectionCells(const QVector<competition::TaskEvent> &events) {
    QStringList result;
    for (const competition::TaskEvent &event : events) {
        if (event.event_type == "detection") {
            result.append(event.waypoint_id);
        }
    }
    return result;
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
    plan.start_waypoint_id = "A9B1";
    plan.terminal_waypoint_id = "touchdown";
    plan.waypoints = {
        {"A9B1", 0, 0.0, 0.0, 1.2, "takeoff", R"({"cell":"A9B1"})"},
        {"A8B1", 1, 0.0, 0.5, 1.2, "navigate", R"({"cell":"A8B1"})"},
        {"A9B1", 2, 0.0, 0.0, 1.2, "navigate", R"({"cell":"A9B1"})"},
        {"touchdown", 3, 0.2, 0.2, 0.0, "land", R"({"touchdown":true})"},
    };

    hcore::CaseConfig config = makeCase();
    config.animals.append({"touchdown", "invalid-terminal-animal", 1});

    QString error;
    const auto stream = hcore::simulateTaskStream(config, plan, &error);
    QVERIFY2(stream.has_value(), qPrintable(error));
    QCOMPARE(stream->plan.task_id, QString("plan-test"));
    QCOMPARE(stream->plan.waypoints.size(), plan.waypoints.size());

    const QVector<competition::TaskEvent> telemetry_events = telemetryEvents(stream->events);
    QCOMPARE(telemetry_events.size(), 4);
    QCOMPARE(stream->summary.visited_waypoints, 4U);
    QCOMPARE(telemetry_events.last().waypoint_id, QString("touchdown"));
    QCOMPARE(telemetryPayload(telemetry_events.last()).value("current_cell").toString(),
             QString("A9B1"));
    QVERIFY(detectionCells(stream->events).contains("touchdown") == false);
}

QTEST_MAIN(HSimulatorTests)
#include "test_h_simulator.moc"
