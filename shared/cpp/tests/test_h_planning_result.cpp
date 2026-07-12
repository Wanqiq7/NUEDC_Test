#include <QtTest/QtTest>

#include "h_problem_core/planning/route_planner.h"

namespace {

hcore::LandingProfile smallGridLandingProfile() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {200.0, 200.0};
    landing_profile.cruise_height_cm = 100.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 0.0;
    landing_profile.preferred_heading_deg = 45.0;
    landing_profile.heading_tolerance_deg = 180.0;
    return landing_profile;
}

}

class HPlanningResultTests : public QObject {
    Q_OBJECT

private slots:
    void routeRequestRequiresLandingProfile();
    void planningResultReportsCoverageAndCost();
    void timeOptimalResultReportsConfiguredMissionTimeAndSearchStatus();
    void planningResultReportsFailureForBlockedStart();
};

void HPlanningResultTests::routeRequestRequiresLandingProfile() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY(!result.ok);
    QVERIFY(result.route.isEmpty());
    QVERIFY(result.failure_reason.contains("landing_profile"));
}

void HPlanningResultTests::planningResultReportsCoverageAndCost() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.no_fly_cells = {"A2B2"};
    request.landing_profile = smallGridLandingProfile();

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY2(result.ok, qPrintable(result.failure_reason));
    QVERIFY(!result.route.isEmpty());
    QCOMPARE(result.route.first(), QString("A1B1"));
    QCOMPARE(result.coverage_rate, 1.0);
    QVERIFY(result.cost > 0.0);
    QCOMPARE(result.cost, result.estimated_mission_time_s);
}

void HPlanningResultTests::timeOptimalResultReportsConfiguredMissionTimeAndSearchStatus() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.landing_profile = smallGridLandingProfile();
    request.mission_timing.cruise_speed_cm_per_s = 50.0;
    request.mission_timing.ascent_speed_cm_per_s = 50.0;
    request.mission_timing.descent_speed_cm_per_s = 50.0;
    request.mission_timing.takeoff_fixed_time_s = 2.0;
    request.mission_timing.landing_fixed_time_s = 3.0;
    request.mission_timing.per_cell_dwell_time_s = 0.5;

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY2(result.ok, qPrintable(result.failure_reason));
    QVERIFY(result.estimated_mission_time_s > 0.0);
    QCOMPARE(result.cost, result.estimated_mission_time_s);
    QCOMPARE(result.search_optimality, hcore::SearchOptimality::ProvenOptimal);
}

void HPlanningResultTests::planningResultReportsFailureForBlockedStart() {
    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.no_fly_cells = {"A1B1"};
    request.landing_profile = smallGridLandingProfile();

    const hcore::PlanningResult result = hcore::planRoute(request);

    QVERIFY(!result.ok);
    QVERIFY(result.route.isEmpty());
    QCOMPARE(result.coverage_rate, 0.0);
    QVERIFY(result.failure_reason.contains("start"));
}

QTEST_MAIN(HPlanningResultTests)
#include "test_h_planning_result.moc"
