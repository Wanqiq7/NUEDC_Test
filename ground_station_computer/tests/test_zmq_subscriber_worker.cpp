#include <QtTest/QtTest>

#include "competition_core/protocol/envelope_codec.h"
#include "framework/communication/zmq_subscriber_worker.h"

#include <QSignalSpy>

#include <memory>

#include <zmq.hpp>

class ZmqSubscriberWorkerTests : public QObject {
    Q_OBJECT

private slots:
    void publishesGenericTaskSignals();
};

void ZmqSubscriberWorkerTests::publishesGenericTaskSignals() {
    const QString endpoint = "tcp://127.0.0.1:56569";
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::pub);
    socket.bind(endpoint.toStdString());

    ZmqSubscriberWorker worker(endpoint);
    QSignalSpy plan_spy(&worker, &ZmqSubscriberWorker::taskPlanReceived);
    QSignalSpy event_spy(&worker, &ZmqSubscriberWorker::taskEventReceived);
    QSignalSpy summary_spy(&worker, &ZmqSubscriberWorker::taskSummaryReceived);
    QSignalSpy error_spy(&worker, &ZmqSubscriberWorker::errorOccurred);

    worker.start();
    QTest::qWait(250);

    competition::TaskPlan plan;
    plan.task_id = "generic-plan";
    plan.task_type = "demo_problem";
    plan.start_waypoint_id = "start";
    plan.terminal_waypoint_id = "end";
    plan.waypoints.append(competition::TaskWaypoint{
        "start",
        0,
        1.0,
        2.0,
        3.0,
        "scan",
        R"({"kind":"demo"})",
    });
    Envelope plan_envelope = competition::buildTaskPlanEnvelope(1, plan);
    std::string plan_bytes;
    QVERIFY(plan_envelope.SerializeToString(&plan_bytes));
    socket.send(zmq::buffer(plan_bytes), zmq::send_flags::none);

    competition::TaskEvent event;
    event.task_id = "generic-plan";
    event.event_type = "item_scanned";
    event.sequence_index = 2;
    event.waypoint_id = "start";
    event.payload_json = R"({"item_id":7})";
    Envelope event_envelope = competition::buildTaskEventEnvelope(2, event);
    std::string event_bytes;
    QVERIFY(event_envelope.SerializeToString(&event_bytes));
    socket.send(zmq::buffer(event_bytes), zmq::send_flags::none);

    competition::TaskSummary summary;
    summary.task_id = "generic-plan";
    summary.task_type = "demo_problem";
    summary.success = true;
    summary.visited_waypoints = 1;
    summary.payload_json = R"({"done":true})";
    Envelope summary_envelope = competition::buildTaskSummaryEnvelope(3, summary);
    std::string summary_bytes;
    QVERIFY(summary_envelope.SerializeToString(&summary_bytes));
    socket.send(zmq::buffer(summary_bytes), zmq::send_flags::none);

    QTRY_COMPARE(plan_spy.size(), 1);
    QTRY_COMPARE(event_spy.size(), 1);
    QTRY_COMPARE(summary_spy.size(), 1);
    QCOMPARE(error_spy.size(), 0);

    const auto received_plan = plan_spy.takeFirst().at(0).value<competition::TaskPlan>();
    QCOMPARE(received_plan.task_type, QString("demo_problem"));
    QCOMPARE(received_plan.waypoints.first().action, QString("scan"));

    const auto received_event = event_spy.takeFirst().at(0).value<competition::TaskEvent>();
    QCOMPARE(received_event.event_type, QString("item_scanned"));
    QCOMPARE(received_event.payload_json, QString(R"({"item_id":7})"));

    const auto received_summary = summary_spy.takeFirst().at(0).value<competition::TaskSummary>();
    QCOMPARE(received_summary.task_type, QString("demo_problem"));
    QCOMPARE(received_summary.visited_waypoints, static_cast<quint32>(1));

    worker.requestInterruption();
    worker.wait(1000);
    socket.close();
    context.close();
}

QTEST_MAIN(ZmqSubscriberWorkerTests)
#include "test_zmq_subscriber_worker.moc"
