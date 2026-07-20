#include <QtTest/QtTest>
#include <QJsonDocument>
#include "h_problem_core/tools/planner_cli.h"

class HRoutePlannerCliTests : public QObject {
    Q_OBJECT
private slots:
    void plansCanonicalTask();
    void rejectsUnknownSchema();
    void rejectsInvalidNoFlyCell();
    void rejectsMissingCase();
};

void HRoutePlannerCliTests::plansCanonicalTask() {
    const QByteArray input = R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["A2B2","A2B3","A2B4"]})";
    const hcore::PlannerCliResult result = hcore::runPlannerCliRequest(input);
    QCOMPARE(result.exit_code, 0);
    const QJsonObject output = QJsonDocument::fromJson(result.stdout_bytes).object();
    QVERIFY(output.value("ok").toBool());
    QCOMPARE(output.value("plan").toObject().value("message_type").toString(), QString("task_plan"));
    QCOMPARE(output.value("plan").toObject().value("terminal_waypoint_id").toString(), QString("touchdown"));
}

void HRoutePlannerCliTests::rejectsUnknownSchema() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"wrong","case_path":"shared/cases/sample_case.json","no_fly_cells":[]})");
    QCOMPARE(result.exit_code, 2);
    QCOMPARE(QJsonDocument::fromJson(result.stdout_bytes).object().value("error_code").toString(), QString("invalid_request"));
}

void HRoutePlannerCliTests::rejectsInvalidNoFlyCell() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/sample_case.json","no_fly_cells":["invalid"]})");
    QCOMPARE(result.exit_code, 3);
    QCOMPARE(QJsonDocument::fromJson(result.stdout_bytes).object().value("ok").toBool(), false);
}

void HRoutePlannerCliTests::rejectsMissingCase() {
    const auto result = hcore::runPlannerCliRequest(R"({"schema":"h_planning_request_v1","case_path":"shared/cases/missing.json","no_fly_cells":[]})");
    QCOMPARE(result.exit_code, 3);
    const QJsonDocument document = QJsonDocument::fromJson(result.stdout_bytes);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value("error_code").toString(), QString("case_load_failed"));
    QVERIFY(result.stderr_bytes.isEmpty() || !result.stderr_bytes.contains('{'));
}

QTEST_MAIN(HRoutePlannerCliTests)
#include "test_h_route_planner_cli.moc"
