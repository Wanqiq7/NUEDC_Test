#include <QtTest/QtTest>

#include "h_problem/rules/h_grid_mapper.h"

class GridMapperTests : public QObject {
    Q_OBJECT

private slots:
    void convertsGridCodesToCoordinates();
};

void GridMapperTests::convertsGridCodesToCoordinates() {
    const auto point = GridMapper::toPoint("A3B5");
    QCOMPARE(point.x(), 2);
    QCOMPARE(point.y(), 4);
    QCOMPARE(GridMapper::toCode(point), QString("A3B5"));
}

QTEST_MAIN(GridMapperTests)
#include "test_grid_mapper.moc"
