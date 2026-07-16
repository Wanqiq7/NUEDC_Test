#include <QtTest/QtTest>

#include "h_problem_core/planning/mission_geometry.h"

class HMissionGeometryTests : public QObject {
    Q_OBJECT

private slots:
    void convertsFieldCentimetersToMissionMeters();
};

void HMissionGeometryTests::convertsFieldCentimetersToMissionMeters() {
    struct Case {
        hcore::PointCm input;
        double expected_x;
        double expected_y;
    };
    const QVector<Case> cases{
        {{450.0, 350.0}, 0.0, 0.0},
        {{50.0, 350.0}, 0.0, 4.0},
        {{450.0, 50.0}, 3.0, 0.0},
        {{50.0, 50.0}, 3.0, 4.0},
        {{440.0, 330.0}, 0.2, 0.1},
    };

    for (const Case &item : cases) {
        const hcore::MissionPointM point = hcore::fieldPointToMissionMeters(item.input);
        QCOMPARE(point.x_m, item.expected_x);
        QCOMPARE(point.y_m, item.expected_y);
    }
}

QTEST_MAIN(HMissionGeometryTests)
#include "test_h_mission_geometry.moc"
