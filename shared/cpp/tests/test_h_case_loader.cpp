#include <QtTest/QtTest>

#include "h_problem_core/mission/case_loader.h"

class HCaseLoaderTests : public QObject {
    Q_OBJECT

private slots:
    void parsesSampleCase();
};

void HCaseLoaderTests::parsesSampleCase() {
    QString error;
    const auto maybe_case = hcore::loadCase("shared/cases/sample_case.json", &error);
    QVERIFY2(maybe_case.has_value(), qPrintable(error));

    const hcore::CaseConfig loaded = maybe_case.value();
    QCOMPARE(loaded.case_id, QString("wildlife-demo"));
    QCOMPARE(loaded.start_cell, QString("A9B1"));
    QCOMPARE(loaded.no_fly_cells, QStringList({"A4B3", "A5B3", "A6B3"}));
    QCOMPARE(loaded.tick_interval_ms, 150);
    QCOMPARE(loaded.animals.size(), 4);
    QVERIFY(loaded.landing.has_value());
    QCOMPARE(loaded.landing->takeoff_anchor_cm.x_cm, 450.0);
    QCOMPARE(loaded.landing->descent_angle_deg, 45.0);
    QVERIFY(!loaded.return_to_start);
}

QTEST_MAIN(HCaseLoaderTests)
#include "test_h_case_loader.moc"
