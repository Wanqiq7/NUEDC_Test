#include <QtTest/QtTest>

#include "h_problem/rules/h_no_fly_zone_rules.h"

class NoFlyZoneRulesTests : public QObject {
    Q_OBJECT

private slots:
    void acceptsHorizontalTriple();
    void acceptsVerticalTriple();
    void rejectsLShape();
    void rejectsGapSelection();
    void rejectsStartCellInclusion();
};

void NoFlyZoneRulesTests::acceptsHorizontalTriple() {
    const QStringList cells = {"A1B1", "A2B1", "A3B1"};
    const auto result = NoFlyZoneRules::validateSelection(cells, "A4B1");
    QVERIFY(result.is_valid);
    QCOMPARE(result.message, QStringLiteral("横向连续三格"));
}

void NoFlyZoneRulesTests::acceptsVerticalTriple() {
    const QStringList cells = {"A2B2", "A2B3", "A2B4"};
    const auto result = NoFlyZoneRules::validateSelection(cells, "A1B1");
    QVERIFY(result.is_valid);
    QCOMPARE(result.message, QStringLiteral("纵向连续三格"));
}

void NoFlyZoneRulesTests::rejectsLShape() {
    const QStringList cells = {"A1B1", "A2B1", "A2B2"};
    const auto result = NoFlyZoneRules::validateSelection(cells, "A3B3");
    QVERIFY(!result.is_valid);
    QCOMPARE(result.message, QStringLiteral("必须横向或纵向连续三格"));
}

void NoFlyZoneRulesTests::rejectsGapSelection() {
    const QStringList cells = {"A1B1", "A3B1", "A4B1"};
    const auto result = NoFlyZoneRules::validateSelection(cells, "A2B2");
    QVERIFY(!result.is_valid);
    QCOMPARE(result.message, QStringLiteral("选区必须连续三格"));
}

void NoFlyZoneRulesTests::rejectsStartCellInclusion() {
    const QStringList cells = {"A1B1", "A2B1", "A3B1"};
    const auto result = NoFlyZoneRules::validateSelection(cells, "A2B1");
    QVERIFY(!result.is_valid);
    QCOMPARE(result.message, QStringLiteral("不能包含起飞格"));
}

QTEST_MAIN(NoFlyZoneRulesTests)
#include "test_no_fly_zone_rules.moc"
