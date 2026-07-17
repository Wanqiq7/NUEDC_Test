#include <QtTest/QtTest>

#include "h_problem/rules/h_grid_mapper.h"

class GridMapperTests : public QObject {
    Q_OBJECT

private slots:
    void convertsGridCodesToCoordinates();
    void rejectsNonGridWaypointIds_data();
    void rejectsNonGridWaypointIds();
};

void GridMapperTests::convertsGridCodesToCoordinates() {
    const auto point = GridMapper::toPoint("A3B5");
    QCOMPARE(point.x(), 2);
    QCOMPARE(point.y(), 4);
    QCOMPARE(GridMapper::toCode(point), QString("A3B5"));

    const auto parsed = GridMapper::tryToPoint("A3B5");
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed.value(), QPoint(2, 4));
}

void GridMapperTests::rejectsNonGridWaypointIds_data() {
    QTest::addColumn<QString>("code");
    QTest::newRow("touchdown") << QStringLiteral("touchdown");
    QTest::newRow("empty") << QString();
    QTest::newRow("zero-row") << QStringLiteral("A0B1");
    QTest::newRow("zero-column") << QStringLiteral("A1B0");
    QTest::newRow("row-out-of-range") << QStringLiteral("A10B1");
    QTest::newRow("column-out-of-range") << QStringLiteral("A1B8");
    QTest::newRow("trailing-data") << QStringLiteral("A1B1-extra");
    QTest::newRow("final-newline") << QStringLiteral("A1B1\n");
}

void GridMapperTests::rejectsNonGridWaypointIds() {
    QFETCH(QString, code);
    QVERIFY(!GridMapper::tryToPoint(code).has_value());
}

QTEST_MAIN(GridMapperTests)
#include "test_grid_mapper.moc"
