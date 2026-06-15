#include <QtTest/QtTest>

#include "competition_core/task/task_ports.h"

namespace {

class DemoPlanner final : public competition::TaskPlanner {
public:
    competition::TaskPlanningResult planTask(const competition::TaskPlanningRequest &request) const override {
        competition::TaskPlan plan;
        plan.task_id = request.task_id;
        plan.task_type = request.task_type;
        plan.metadata_json = request.metadata_json;
        return competition::TaskPlanningResult::success(plan);
    }
};

class DemoCodec final : public competition::TaskCodec {
public:
    QString adapterId() const override { return "demo"; }
    QString encodeMetadata(const QVariantMap &metadata) const override {
        return QString("adapter=%1;case=%2").arg(adapterId(), metadata.value("case_id").toString());
    }
};

}

class TaskPortsTests : public QObject {
    Q_OBJECT

private slots:
    void plannerPortReturnsGenericTaskPlan();
    void codecPortKeepsAdapterSpecificDataBehindJsonBoundary();
    void planningResultCanRepresentFailure();
};

void TaskPortsTests::plannerPortReturnsGenericTaskPlan() {
    DemoPlanner planner;

    competition::TaskPlanningRequest request;
    request.adapter_id = "demo";
    request.task_id = "task-001";
    request.task_type = "demo_task";
    request.metadata_json = R"({"adapter_id":"demo"})";

    const competition::TaskPlanningResult result = planner.planTask(request);

    QVERIFY(result.ok);
    QCOMPARE(result.plan.task_id, QString("task-001"));
    QCOMPARE(result.plan.task_type, QString("demo_task"));
    QVERIFY(result.failure_reason.isEmpty());
}

void TaskPortsTests::codecPortKeepsAdapterSpecificDataBehindJsonBoundary() {
    DemoCodec codec;

    QVariantMap metadata;
    metadata.insert("case_id", "sample");

    QCOMPARE(codec.adapterId(), QString("demo"));
    QCOMPARE(codec.encodeMetadata(metadata), QString("adapter=demo;case=sample"));
}

void TaskPortsTests::planningResultCanRepresentFailure() {
    const competition::TaskPlanningResult result =
        competition::TaskPlanningResult::failure("no reachable route");

    QVERIFY(!result.ok);
    QCOMPARE(result.failure_reason, QString("no reachable route"));
    QVERIFY(result.plan.task_id.isEmpty());
}

QTEST_MAIN(TaskPortsTests)
#include "test_task_ports.moc"
