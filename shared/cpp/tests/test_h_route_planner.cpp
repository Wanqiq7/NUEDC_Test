#include <QtTest/QtTest>

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"
#include "h_problem_core/planning/route_planner.h"

namespace {

QPoint toPoint(const QString &cell) {
    QString error;
    const auto decoded = hcore::decodeCell(cell, &error);
    Q_ASSERT(decoded.has_value());
    return decoded.value();
}

void verifyAdjacentRoute(const QStringList &route) {
    for (int index = 1; index < route.size(); ++index) {
        const QPoint previous = toPoint(route.at(index - 1));
        const QPoint current = toPoint(route.at(index));
        QCOMPARE(std::abs(previous.x() - current.x()) + std::abs(previous.y() - current.y()), 1);
    }
}

}

class HRoutePlannerTests : public QObject {
    Q_OBJECT

private slots:
    void coversEveryNonForbiddenCellWithAdjacentSteps();
    void keepsSampleCaseRouteLengthAtOrBelow61();
    void supportsClosedTourBackToStart();
    void prefersLandingCompatibleTerminalRegion();
    void timeOptimalOpenRouteBeatsClosedCycleCost();
    void findsExactPathForSmallGrid();
    void exactCompletionToEndFindsShortestTailOnSmallGrid();
    void estimateRouteCostPenalizesDetoursAndTurns();
};

void HRoutePlannerTests::coversEveryNonForbiddenCellWithAdjacentSteps() {
    QString error;
    const QSet<QString> blocked{"A4B3", "A5B3", "A6B3"};
    const QStringList route = hcore::planRoute(9, 7, "A1B1", blocked, {}, false, hcore::MissionMode::Legacy, {}, &error);

    QVERIFY2(!route.isEmpty(), qPrintable(error));
    QCOMPARE(route.first(), QString("A1B1"));
    for (const QString &blocked_cell : blocked) {
        QVERIFY(!route.contains(blocked_cell));
    }
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    verifyAdjacentRoute(route);
}

void HRoutePlannerTests::keepsSampleCaseRouteLengthAtOrBelow61() {
    QString error;
    const QStringList route = hcore::planRoute(9, 7, "A1B1", {"A4B3", "A5B3", "A6B3"}, {}, false, hcore::MissionMode::Legacy, {}, &error);
    QVERIFY2(!route.isEmpty(), qPrintable(error));
    QVERIFY2(route.size() <= 61, qPrintable(QString("route length regressed to %1").arg(route.size())));
}

void HRoutePlannerTests::supportsClosedTourBackToStart() {
    QString error;
    const QStringList route = hcore::planRoute(9, 7, "A9B7", {"A4B3", "A5B3", "A6B3"}, QString("A9B7"), true, hcore::MissionMode::Legacy, {}, &error);
    QVERIFY2(!route.isEmpty(), qPrintable(error));
    QCOMPARE(route.first(), QString("A9B7"));
    QCOMPARE(route.last(), QString("A9B7"));
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    verifyAdjacentRoute(route);
}

void HRoutePlannerTests::prefersLandingCompatibleTerminalRegion() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {450.0, 350.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;

    QString error;
    const QSet<QString> blocked{"A4B3", "A5B3", "A6B3"};
    const QStringList route = hcore::planRoute(9, 7, "A9B1", blocked, {}, false, hcore::MissionMode::TimeOptimalOpen, landing_profile, &error);
    QVERIFY2(!route.isEmpty(), qPrintable(error));

    const QSet<QString> terminal_cells = hcore::terminalCellsForLanding(9, 7, blocked, landing_profile);
    QVERIFY2(terminal_cells.contains(route.last()), qPrintable(QString("terminal cell %1 was not landing compatible").arg(route.last())));
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    verifyAdjacentRoute(route);
}

void HRoutePlannerTests::timeOptimalOpenRouteBeatsClosedCycleCost() {
    QString error;
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {450.0, 350.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;

    const QSet<QString> blocked{"A4B3", "A5B3", "A6B3"};
    const QStringList closed_route = hcore::planRoute(9, 7, "A9B7", blocked, QString("A9B7"), true, hcore::MissionMode::Legacy, {}, &error);
    QVERIFY2(!closed_route.isEmpty(), qPrintable(error));
    const QStringList open_route = hcore::planRoute(9, 7, "A9B1", blocked, {}, false, hcore::MissionMode::TimeOptimalOpen, landing_profile, &error);
    QVERIFY2(!open_route.isEmpty(), qPrintable(error));

    QVERIFY(hcore::estimateRouteCost(open_route, 7, 18.0, 6.0, landing_profile, 9, blocked)
        < hcore::estimateRouteCost(closed_route, 7, 18.0, 6.0, landing_profile, 9, blocked));
}

void HRoutePlannerTests::findsExactPathForSmallGrid() {
    QString error;
    const QStringList route = hcore::planRoute(4, 4, "A1B1", {"A4B1"}, {}, false, hcore::MissionMode::Legacy, {}, &error);
    QVERIFY2(!route.isEmpty(), qPrintable(error));
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 15);
    QCOMPARE(route.size(), 15);
}

void HRoutePlannerTests::exactCompletionToEndFindsShortestTailOnSmallGrid() {
    const QStringList completion = hcore::exactCompletionToEndForTesting(
        3,
        3,
        "A1B1",
        QStringList{"A2B1", "A3B1"},
        "A3B3",
        {});

    QCOMPARE(completion, QStringList({"A1B1", "A2B1", "A3B1", "A3B2", "A3B3"}));
}

void HRoutePlannerTests::estimateRouteCostPenalizesDetoursAndTurns() {
    const QStringList direct_route{"A1B1", "A2B1", "A3B1"};
    const QStringList detour_route{"A1B1", "A1B2", "A2B2", "A2B1", "A3B1"};

    QVERIFY(hcore::estimateRouteCost(direct_route) < hcore::estimateRouteCost(detour_route));
}

QTEST_MAIN(HRoutePlannerTests)
#include "test_h_route_planner.moc"
