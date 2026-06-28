#include <QtTest/QtTest>

#include "h_problem_core/planning/route_planner.h"

class HPlanningResultTests : public QObject {
    Q_OBJECT

private slots:
    void routeRequestPlansLegacyRoute();
    void routeRequestRequiresLandingProfileForTimeOptimalOpen();
    void planningResultReportsCoverageAndCost();
    void planningResultReportsFailureForBlockedStart();
};

void HPlanningResultTests::routeRequestPlansLegacyRoute() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.no_fly_cells = {"A2B2"};
    request.mission_mode = hcore::MissionMode::Legacy;

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY2(result.ok, qPrintable(result.failure_reason));
    QCOMPARE(result.route.first(), QString("A1B1"));
    QCOMPARE(result.coverage_rate, 1.0);
    QVERIFY(result.cost > 0.0);
}

void HPlanningResultTests::routeRequestRequiresLandingProfileForTimeOptimalOpen() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.mission_mode = hcore::MissionMode::TimeOptimalOpen;

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY(!result.ok);
    QVERIFY(result.route.isEmpty());
    QVERIFY(result.failure_reason.contains("landing_profile"));
}

void HPlanningResultTests::planningResultReportsCoverageAndCost() {
    const hcore::PlanningResult result = hcore::planRouteWithDetails(
        3,
        3,
        "A1B1",
        {"A2B2"},
        {},
        false,
        hcore::MissionMode::Legacy);

    QVERIFY2(result.ok, qPrintable(result.failure_reason));
    QVERIFY(!result.route.isEmpty());
    QCOMPARE(result.route.first(), QString("A1B1"));
    QCOMPARE(result.coverage_rate, 1.0);
    QVERIFY(result.cost > 0.0);
    QVERIFY(result.warnings.isEmpty());
}

void HPlanningResultTests::planningResultReportsFailureForBlockedStart() {
    const hcore::PlanningResult result = hcore::planRouteWithDetails(
        3,
        3,
        "A1B1",
        {"A1B1"},
        {},
        false,
        hcore::MissionMode::Legacy);

    QVERIFY(!result.ok);
    QVERIFY(result.route.isEmpty());
    QCOMPARE(result.coverage_rate, 0.0);
    QVERIFY(result.failure_reason.contains("start"));
}

QTEST_MAIN(HPlanningResultTests)
#include "test_h_planning_result.moc"
