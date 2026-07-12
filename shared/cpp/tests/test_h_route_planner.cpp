#include <QtTest/QtTest>

#include "h_problem_core/planning/mission_geometry.h"
#include "h_problem_core/planning/route_cost.h"
#include "h_problem_core/planning/route_planner.h"

#include <QHash>
#include <QQueue>

#include <cmath>
#include <limits>

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

hcore::LandingProfile landingProfileForTerminal(const QString &terminal_cell, int height) {
    const auto terminal_center = hcore::cellCodeCenterCm(terminal_cell, height);
    Q_ASSERT(terminal_center.has_value());

    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {
        terminal_center->x_cm + 100.0,
        terminal_center->y_cm + 100.0,
    };
    landing_profile.cruise_height_cm = 140.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;
    landing_profile.preferred_heading_deg = 45.0;
    landing_profile.heading_tolerance_deg = 5.0;
    return landing_profile;
}

hcore::LandingProfile sampleLandingProfile() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {450.0, 350.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;
    return landing_profile;
}

QSet<QString> landingTerminalCellsByIndependentEnumeration(
    int width,
    int height,
    const QSet<QString> &blocked_cells,
    const hcore::LandingProfile &landing_profile) {
    QSet<QString> terminal_cells;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = hcore::encodeCell(x_index, y_index);
            if (blocked_cells.contains(cell)) {
                continue;
            }
            if (hcore::landingApproachForTerminal(
                    width,
                    height,
                    cell,
                    blocked_cells,
                    landing_profile).has_value()) {
                terminal_cells.insert(cell);
            }
        }
    }
    return terminal_cells;
}

QStringList exactTimeOptimalRoute(
    int width,
    int height,
    const QString &start_cell,
    const QSet<QString> &blocked_cells,
    const QSet<QString> &terminal_cells,
    const hcore::LandingProfile &landing_profile,
    const hcore::MissionTiming &mission_timing) {
    constexpr int StatePositionBits = 6;
    constexpr quint64 StatePositionMask = (quint64{1} << StatePositionBits) - 1;
    QVector<QString> cells;
    QMap<QString, int> index_by_cell;
    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index < width; ++x_index) {
            const QString cell = hcore::encodeCell(x_index, y_index);
            if (!blocked_cells.contains(cell)) {
                index_by_cell.insert(cell, cells.size());
                cells.append(cell);
            }
        }
    }
    if (cells.size() > 16 || !index_by_cell.contains(start_cell)) {
        return {};
    }

    QVector<QVector<int>> neighbors(cells.size());
    for (int index = 0; index < cells.size(); ++index) {
        const QPoint point = toPoint(cells.at(index));
        const QVector<QPoint> candidates{
            {point.x() + 1, point.y()},
            {point.x() - 1, point.y()},
            {point.x(), point.y() + 1},
            {point.x(), point.y() - 1},
        };
        for (const QPoint &candidate : candidates) {
            const QString cell = hcore::encodeCell(candidate.x(), candidate.y());
            if (index_by_cell.contains(cell)) {
                neighbors[index].append(index_by_cell.value(cell));
            }
        }
    }

    const int start_index = index_by_cell.value(start_cell);
    const quint64 full_mask = (quint64{1} << cells.size()) - 1;
    QSet<int> unresolved_terminal_indices;
    for (const QString &terminal_cell : terminal_cells) {
        if (index_by_cell.contains(terminal_cell)) {
            unresolved_terminal_indices.insert(index_by_cell.value(terminal_cell));
        }
    }
    if (unresolved_terminal_indices.isEmpty()) {
        return {};
    }

    const auto stateKey = [](quint64 covered, int position) {
        return (covered << StatePositionBits) | static_cast<quint64>(position);
    };
    const quint64 start_state = stateKey(quint64{1} << start_index, start_index);
    QHash<quint64, quint64> parent_by_state;
    parent_by_state.insert(start_state, start_state);
    QQueue<quint64> queue;
    queue.enqueue(start_state);
    QMap<int, quint64> terminal_state_by_index;

    while (!queue.isEmpty()) {
        const quint64 state = queue.dequeue();
        const int position = static_cast<int>(state & StatePositionMask);
        const quint64 covered = state >> StatePositionBits;
        if (covered == full_mask && unresolved_terminal_indices.contains(position)) {
            terminal_state_by_index.insert(position, state);
            unresolved_terminal_indices.remove(position);
            if (unresolved_terminal_indices.isEmpty()) {
                break;
            }
        }
        for (const int neighbor : neighbors.at(position)) {
            const quint64 next_state = stateKey(covered | (quint64{1} << neighbor), neighbor);
            if (parent_by_state.contains(next_state)) {
                continue;
            }
            parent_by_state.insert(next_state, state);
            queue.enqueue(next_state);
        }
    }

    const auto routeForState = [&](quint64 terminal_state) {
        QStringList route;
        quint64 current_state = terminal_state;
        while (true) {
            route.prepend(cells.at(static_cast<int>(current_state & StatePositionMask)));
            if (current_state == start_state) {
                break;
            }
            current_state = parent_by_state.value(current_state);
        }
        return route;
    };

    QStringList best_route;
    for (auto iterator = terminal_state_by_index.cbegin(); iterator != terminal_state_by_index.cend(); ++iterator) {
        const QStringList candidate_route = routeForState(iterator.value());
        const double candidate_time_s = hcore::estimateMissionTimeSeconds(
            candidate_route,
            height,
            landing_profile,
            width,
            blocked_cells,
            mission_timing);
        const double best_time_s = best_route.isEmpty()
            ? std::numeric_limits<double>::infinity()
            : hcore::estimateMissionTimeSeconds(
                  best_route,
                  height,
                  landing_profile,
                  width,
                  blocked_cells,
                  mission_timing);
        if (candidate_time_s < best_time_s - 1e-9
            || (std::abs(candidate_time_s - best_time_s) <= 1e-9 && candidate_route < best_route)) {
            best_route = candidate_route;
        }
    }
    return best_route;
}

}

class HRoutePlannerTests : public QObject {
    Q_OBJECT

private slots:
    void prefersLandingCompatibleTerminalRegion();
    void rejectsLandingTerminalWhoseDescentCorridorCrossesNoFlyCell();
    void descentCorridorHelperRejectsBlockedSegment();
    void touchdownRadiusCanMakeAnOtherwiseTooShortApproachFeasible();
    void missionTimeIncludesTakeoffAnchorTransit();
    void timeOptimalOpenRejectsTakeoffTransitAcrossNoFlyCell();
    void timeOptimalOpenFailsWhenNoLandingCompatibleTerminalExists();
    void timeOptimalSampleRouteImprovesOn68WaypointBaseline();
    void timeOptimalOpenMatchesExactMissionTimeOnSmallGrids_data();
    void timeOptimalOpenMatchesExactMissionTimeOnSmallGrids();
    void timeOptimalOpenCoversAllLegalFullSizeBarrierLayouts_data();
    void timeOptimalOpenCoversAllLegalFullSizeBarrierLayouts();
    void missionTimeDoesNotPenalizeTurnsOrRepeatedVisits();
};

void HRoutePlannerTests::prefersLandingCompatibleTerminalRegion() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {450.0, 350.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;

    QString error;
    const QSet<QString> blocked{"A4B3", "A5B3", "A6B3"};
    const QStringList route = hcore::planRoute(9, 7, "A9B1", blocked, landing_profile, &error);
    QVERIFY2(!route.isEmpty(), qPrintable(error));

    const QSet<QString> terminal_cells = hcore::terminalCellsForLanding(9, 7, blocked, landing_profile);
    QVERIFY2(terminal_cells.contains(route.last()), qPrintable(QString("terminal cell %1 was not landing compatible").arg(route.last())));
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    verifyAdjacentRoute(route);
}

void HRoutePlannerTests::rejectsLandingTerminalWhoseDescentCorridorCrossesNoFlyCell() {
    const hcore::LandingProfile landing_profile = sampleLandingProfile();
    const QSet<QString> blocked{"A8B2"};

    const QSet<QString> terminal_cells = hcore::terminalCellsForLanding(
        9,
        7,
        blocked,
        landing_profile);

    QVERIFY2(
        !terminal_cells.contains("A7B3"),
        "the descent from A7B3 to the takeoff area crosses A8B2");
}

void HRoutePlannerTests::descentCorridorHelperRejectsBlockedSegment() {
    const auto descent_start = hcore::cellCodeCenterCm("A7B3", 7);
    QVERIFY(descent_start.has_value());
    const hcore::PointCm touchdown{450.0, 350.0};

    QVERIFY(!hcore::descentCorridorIsClear(
        9,
        7,
        descent_start.value(),
        touchdown,
        {"A8B2"}));
    QVERIFY(hcore::descentCorridorIsClear(
        9,
        7,
        descent_start.value(),
        touchdown,
        {}));
}

void HRoutePlannerTests::touchdownRadiusCanMakeAnOtherwiseTooShortApproachFeasible() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {115.0, 115.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.preferred_heading_deg = 45.0;
    landing_profile.heading_tolerance_deg = 0.0;

    QVERIFY(!hcore::landingApproachForTerminal(
        5,
        5,
        "A1B5",
        {},
        landing_profile).has_value());

    landing_profile.touchdown_radius_cm = 10.0;
    const auto approach = hcore::landingApproachForTerminal(
        5,
        5,
        "A1B5",
        {},
        landing_profile);
    QVERIFY(approach.has_value());
    QVERIFY(approach->horizontal_run_cm >= hcore::computeDescentRunBoundsCm(120.0, 45.0).first);
}

void HRoutePlannerTests::missionTimeIncludesTakeoffAnchorTransit() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {150.0, 150.0};
    landing_profile.cruise_height_cm = 100.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 0.0;
    landing_profile.preferred_heading_deg = 0.0;
    landing_profile.heading_tolerance_deg = 0.0;

    hcore::MissionTiming timing;
    timing.cruise_speed_cm_per_s = 100.0;
    timing.ascent_speed_cm_per_s = 100.0;
    timing.descent_speed_cm_per_s = 100.0;

    const double mission_time_s = hcore::estimateMissionTimeSeconds(
        {"A1B1"},
        3,
        landing_profile,
        3,
        {},
        timing);

    const double expected_time_s = 1.0 + 1.0 + std::sqrt(2.0);
    QVERIFY2(
        std::abs(mission_time_s - expected_time_s) <= 1e-9,
        qPrintable(QString("expected %1 s, got %2 s").arg(expected_time_s).arg(mission_time_s)));
}

void HRoutePlannerTests::timeOptimalOpenRejectsTakeoffTransitAcrossNoFlyCell() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {150.0, 150.0};
    landing_profile.cruise_height_cm = 100.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 0.0;
    landing_profile.preferred_heading_deg = 0.0;
    landing_profile.heading_tolerance_deg = 0.0;

    hcore::RouteRequest request;
    request.width = 3;
    request.height = 3;
    request.start_cell = "A1B1";
    request.no_fly_cells = {"A2B1"};
    request.landing_profile = landing_profile;

    const hcore::RoutePlanResult result = hcore::planRoute(request);

    QVERIFY(!result.ok);
    QVERIFY(result.route.isEmpty());
    QVERIFY(result.failure_reason.contains("takeoff transit"));
}

void HRoutePlannerTests::timeOptimalOpenFailsWhenNoLandingCompatibleTerminalExists() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {10000.0, 10000.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 0.0;

    QString error;
    const QStringList route = hcore::planRoute(
        9,
        7,
        "A9B1",
        {"A4B3", "A5B3", "A6B3"},
        landing_profile,
        &error);

    QVERIFY(route.isEmpty());
    QVERIFY(error.contains("landing-compatible"));
}

void HRoutePlannerTests::timeOptimalSampleRouteImprovesOn68WaypointBaseline() {
    hcore::LandingProfile landing_profile;
    landing_profile.takeoff_anchor_cm = {450.0, 350.0};
    landing_profile.cruise_height_cm = 120.0;
    landing_profile.descent_angle_deg = 45.0;
    landing_profile.touchdown_radius_cm = 18.0;

    QString error;
    const QSet<QString> blocked{"A4B3", "A5B3", "A6B3"};
    const QStringList route = hcore::planRoute(
        9,
        7,
        "A9B1",
        blocked,
        landing_profile,
        &error);

    QVERIFY2(!route.isEmpty(), qPrintable(error));
    QCOMPARE(route.first(), QString("A9B1"));
    QVERIFY2(
        route.size() == 61,
        qPrintable(QString("expected 61 nodes, got %1 ending at %2 with estimated time %3 s")
                       .arg(route.size())
                       .arg(route.last())
                       .arg(hcore::estimateMissionTimeSeconds(route, 7, landing_profile, 9, blocked), 0, 'f', 3)
                    + QString("; landing terminals: %1")
                          .arg(hcore::terminalCellsForLanding(9, 7, blocked, landing_profile).values().join(','))));
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    for (const QString &blocked_cell : blocked) {
        QVERIFY(!route.contains(blocked_cell));
    }
    QVERIFY(hcore::terminalCellsForLanding(9, 7, blocked, landing_profile).contains(route.last()));
    verifyAdjacentRoute(route);
}

void HRoutePlannerTests::timeOptimalOpenMatchesExactMissionTimeOnSmallGrids_data() {
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<QString>("start_cell");
    QTest::addColumn<QStringList>("blocked_cells");
    QTest::addColumn<QString>("terminal_cell");

    QTest::newRow("three_by_three_open")
        << 3 << 3 << QString("A1B1") << QStringList{} << QString("A3B3");
    QTest::newRow("three_by_three_center_blocked")
        << 3 << 3 << QString("A1B1") << QStringList{"A2B2"} << QString("A3B3");
    QTest::newRow("four_by_three_blocked")
        << 4 << 3 << QString("A1B1") << QStringList{"A2B2"} << QString("A4B3");
}

void HRoutePlannerTests::timeOptimalOpenMatchesExactMissionTimeOnSmallGrids() {
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(QString, start_cell);
    QFETCH(QStringList, blocked_cells);
    QFETCH(QString, terminal_cell);

    const QSet<QString> blocked(blocked_cells.begin(), blocked_cells.end());
    const hcore::LandingProfile landing_profile = landingProfileForTerminal(terminal_cell, height);
    const QSet<QString> terminal_cells = hcore::terminalCellsForLanding(width, height, blocked, landing_profile);
    QVERIFY2(terminal_cells.contains(terminal_cell), "test landing profile must admit its requested terminal");

    hcore::MissionTiming mission_timing;
    mission_timing.cruise_speed_cm_per_s = 75.0;
    mission_timing.ascent_speed_cm_per_s = 60.0;
    mission_timing.descent_speed_cm_per_s = 55.0;
    mission_timing.takeoff_fixed_time_s = 1.0;
    mission_timing.landing_fixed_time_s = 2.0;
    mission_timing.per_cell_dwell_time_s = 0.25;

    hcore::RouteRequest request;
    request.width = width;
    request.height = height;
    request.start_cell = start_cell;
    request.no_fly_cells = blocked;
    request.landing_profile = landing_profile;
    request.mission_timing = mission_timing;
    const hcore::RoutePlanResult planned_result = hcore::planRoute(request);
    QVERIFY2(planned_result.ok, qPrintable(planned_result.failure_reason));
    QCOMPARE(planned_result.search_optimality, hcore::SearchOptimality::ProvenOptimal);
    const QStringList exact_route = exactTimeOptimalRoute(
        width,
        height,
        start_cell,
        blocked,
        terminal_cells,
        landing_profile,
        mission_timing);
    QVERIFY2(!exact_route.isEmpty(), "small-grid exact baseline did not find a route");

    const double planned_time_s = planned_result.estimated_mission_time_s;
    const double exact_time_s = hcore::estimateMissionTimeSeconds(
        exact_route,
        height,
        landing_profile,
        width,
        blocked,
        mission_timing);
    QVERIFY2(
        std::abs(planned_time_s - exact_time_s) <= 1e-9,
        qPrintable(QString("planner time %1 differs from exact %2").arg(planned_time_s).arg(exact_time_s)));
}

void HRoutePlannerTests::timeOptimalOpenCoversAllLegalFullSizeBarrierLayouts_data() {
    QTest::addColumn<QStringList>("blocked_cells");

    constexpr int width = 9;
    constexpr int height = 7;
    const QString start_cell = "A9B1";
    const auto addRow = [&](const QString &tag, const QStringList &cells) {
        if (cells.contains(start_cell)) {
            return;
        }
        const QByteArray tag_bytes = tag.toUtf8();
        QTest::newRow(tag_bytes.constData()) << cells;
    };

    for (int y_index = 0; y_index < height; ++y_index) {
        for (int x_index = 0; x_index <= width - 3; ++x_index) {
            addRow(
                QString("horizontal_A%1B%2").arg(x_index + 1).arg(y_index + 1),
                {
                    hcore::encodeCell(x_index, y_index),
                    hcore::encodeCell(x_index + 1, y_index),
                    hcore::encodeCell(x_index + 2, y_index),
                });
        }
    }
    for (int x_index = 0; x_index < width; ++x_index) {
        for (int y_index = 0; y_index <= height - 3; ++y_index) {
            addRow(
                QString("vertical_A%1B%2").arg(x_index + 1).arg(y_index + 1),
                {
                    hcore::encodeCell(x_index, y_index),
                    hcore::encodeCell(x_index, y_index + 1),
                    hcore::encodeCell(x_index, y_index + 2),
                });
        }
    }
}

void HRoutePlannerTests::timeOptimalOpenCoversAllLegalFullSizeBarrierLayouts() {
    QFETCH(QStringList, blocked_cells);

    const QSet<QString> blocked(blocked_cells.begin(), blocked_cells.end());
    const hcore::LandingProfile landing_profile = sampleLandingProfile();
    hcore::RouteRequest request;
    request.width = 9;
    request.height = 7;
    request.start_cell = "A9B1";
    request.no_fly_cells = blocked;
    request.landing_profile = landing_profile;
    const hcore::RoutePlanResult result = hcore::planRoute(request);
    const QSet<QString> expected_terminal_cells = landingTerminalCellsByIndependentEnumeration(
        request.width,
        request.height,
        blocked,
        landing_profile);

    QVERIFY2(
        !expected_terminal_cells.isEmpty(),
        qPrintable(QString("legal triomino layout %1 has no landing-compatible terminal")
                       .arg(QString::fromUtf8(QTest::currentDataTag()))));
    QVERIFY2(result.ok, qPrintable(result.failure_reason));
    const QStringList &route = result.route;
    QCOMPARE(route.first(), request.start_cell);
    QCOMPARE(QSet<QString>(route.begin(), route.end()).size(), 60);
    QVERIFY(expected_terminal_cells.contains(route.last()));
    for (const QString &blocked_cell : blocked) {
        QVERIFY(!route.contains(blocked_cell));
    }
    verifyAdjacentRoute(route);
    QVERIFY(result.estimated_mission_time_s < 300.0);
}

void HRoutePlannerTests::missionTimeDoesNotPenalizeTurnsOrRepeatedVisits() {
    const QStringList straight_route{"A1B1", "A2B1", "A3B1"};
    const QStringList turning_route{"A1B1", "A1B2", "A2B2"};
    const QStringList repeated_route{"A1B1", "A2B1", "A1B1"};

    const double straight_time = hcore::estimateMissionTimeSeconds(straight_route);
    QCOMPARE(hcore::estimateMissionTimeSeconds(turning_route), straight_time);
    QCOMPARE(hcore::estimateMissionTimeSeconds(repeated_route), straight_time);
}

QTEST_MAIN(HRoutePlannerTests)
#include "test_h_route_planner.moc"
