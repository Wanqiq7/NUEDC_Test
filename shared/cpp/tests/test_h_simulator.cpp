#include <QtTest/QtTest>

#include "h_problem_core/runtime/simulator.h"

namespace {

hcore::CaseConfig makeCase() {
    hcore::CaseConfig config;
    config.case_id = "plan-test";
    config.start_cell = "A1B1";
    config.tick_interval_ms = 10;
    config.animals.append({"A2B1", "elephant", 1});
    config.animals.append({"A3B1", "monkey", 2});
    return config;
}

}

class HSimulatorTests : public QObject {
    Q_OBJECT

private slots:
    void emitsDetectionAndSummaryForAnimals();
    void emitsGenericTaskEventStream();
    void prefersProvidedMissionPlan();
};

void HSimulatorTests::emitsDetectionAndSummaryForAnimals() {
    QString error;
    const QVector<hcore::SimMessage> messages = hcore::simulateMessages(makeCase(), {}, &error);
    QVERIFY2(!messages.isEmpty(), qPrintable(error));

    int detection_count = 0;
    for (const hcore::SimMessage &message : messages) {
        if (message.type == hcore::SimMessageType::Detection) {
            ++detection_count;
        }
    }

    QCOMPARE(detection_count, 2);
    QCOMPARE(messages.last().type, hcore::SimMessageType::Summary);
    QCOMPARE(messages.last().totals.value("elephant"), 1u);
    QCOMPARE(messages.last().totals.value("monkey"), 2u);
}

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
}

void HSimulatorTests::prefersProvidedMissionPlan() {
    hcore::MissionPlan plan;
    plan.case_id = "plan-test";
    plan.start_cell = "A1B1";
    plan.no_fly_cells = {"A1B2"};
    plan.route = {"A1B1", "A2B1", "A3B1"};
    plan.terminal_cell = "A3B1";

    QString error;
    const QVector<hcore::SimMessage> messages = hcore::simulateMessages(makeCase(), plan, &error);
    QVERIFY2(!messages.isEmpty(), qPrintable(error));
    QCOMPARE(messages.first().type, hcore::SimMessageType::Config);
    QCOMPARE(messages.first().mission_plan.route, plan.route);

    QStringList telemetry_cells;
    for (const hcore::SimMessage &message : messages) {
        if (message.type == hcore::SimMessageType::Telemetry) {
            telemetry_cells.append(message.cell);
        }
    }
    QCOMPARE(telemetry_cells, plan.route);
}

QTEST_MAIN(HSimulatorTests)
#include "test_h_simulator.moc"
