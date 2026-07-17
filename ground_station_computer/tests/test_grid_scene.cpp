#include <QtTest/QtTest>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>

#include "h_problem/rules/h_grid_mapper.h"
#include "h_problem/ui/h_grid_scene.h"

namespace {
constexpr int kTestRows = 7;
constexpr qreal kTestCellSize = 52.0;
constexpr int kCurrentMarkerDataKey = 1001;
constexpr const char *kCurrentMarkerDataValue = "current_marker";
constexpr int kLandingRoleDataKey = 1002;

QPointF testCellCenter(const QString &code) {
    const QPoint point = GridMapper::toPoint(code);
    const qreal scene_y_index = (kTestRows - 1) - point.y();
    return QPointF((point.x() * kTestCellSize) + (kTestCellSize / 2.0),
                   (scene_y_index * kTestCellSize) + (kTestCellSize / 2.0));
}

QGraphicsEllipseItem *findCurrentMarker(const GridScene &scene) {
    for (auto *item : scene.items()) {
        if (item->data(kCurrentMarkerDataKey).toString() == QLatin1String(kCurrentMarkerDataValue)) {
            return qgraphicsitem_cast<QGraphicsEllipseItem *>(item);
        }
    }
    return nullptr;
}

QGraphicsItem *findItemByRole(const GridScene &scene, const QString &role) {
    for (QGraphicsItem *item : scene.items()) {
        if (item->data(kLandingRoleDataKey).toString() == role) {
            return item;
        }
    }
    return nullptr;
}

int countItemsByRole(const GridScene &scene, const QString &role) {
    int count = 0;
    for (QGraphicsItem *item : scene.items()) {
        if (item->data(kLandingRoleDataKey).toString() == role) {
            ++count;
        }
    }
    return count;
}

QGraphicsSimpleTextItem *findSimpleText(
    const GridScene &scene, const QString &text) {
    for (QGraphicsItem *item : scene.items()) {
        auto *label = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
        if (label != nullptr && label->text() == text) {
            return label;
        }
    }
    return nullptr;
}

int countSimpleText(const GridScene &scene, const QString &text) {
    int count = 0;
    for (QGraphicsItem *item : scene.items()) {
        auto *label = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
        if (label != nullptr && label->text() == text) {
            ++count;
        }
    }
    return count;
}
}

class TestableGridScene : public GridScene {
public:
    using GridScene::mousePressEvent;
};

class GridSceneTests : public QObject {
    Q_OBJECT

private slots:
    void storesCandidateAndOfficialStatesIndependently();
    void clickingWithoutEditingDoesNotEmitSignal();
    void clickingWithEditingEmitsSelectedCellCode();
    void reusesCurrentMarkerWhenCurrentCellChanges();
    void hidesCurrentMarkerForNonGridWaypoint();
    void drawsSeparateDescentStartAndTouchdownMarkers();
    void zeroTouchdownCoordinatesRemainValid_data();
    void zeroTouchdownCoordinatesRemainValid();
    void clearsLandingItemsWhenDisabled();
};

void GridSceneTests::storesCandidateAndOfficialStatesIndependently() {
    TestableGridScene scene;
    const QStringList official_cells = {"A1B1", "A1B2"};
    const QStringList candidate_cells = {"A2B1"};

    scene.setNoFlyCells(official_cells);
    scene.setCandidateNoFlyCells(candidate_cells);

    QCOMPARE(scene.noFlyCells(), official_cells);
    QCOMPARE(scene.candidateNoFlyCells(), candidate_cells);
    QVERIFY(scene.noFlyCells() != scene.candidateNoFlyCells());
}

void GridSceneTests::clickingWithoutEditingDoesNotEmitSignal() {
    TestableGridScene scene;
    QSignalSpy spy(&scene, &GridScene::cellClicked);
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
    event.setScenePos(testCellCenter("A1B1"));
    event.setButton(Qt::LeftButton);
    event.setButtons(Qt::LeftButton);
    scene.mousePressEvent(&event);
    QCOMPARE(spy.count(), 0);
}

void GridSceneTests::clickingWithEditingEmitsSelectedCellCode() {
    TestableGridScene scene;
    scene.setNoFlyEditEnabled(true);
    scene.setCandidateNoFlyCells({"A1B1"});
    QSignalSpy spy(&scene, &GridScene::cellClicked);
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
    event.setScenePos(testCellCenter("A1B1"));
    event.setButton(Qt::LeftButton);
    event.setButtons(Qt::LeftButton);
    scene.mousePressEvent(&event);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QString("A1B1"));
}

void GridSceneTests::reusesCurrentMarkerWhenCurrentCellChanges() {
    TestableGridScene scene;

    scene.setCurrentCell("A1B1");
    const int item_count_after_first_update = scene.items().size();
    auto *first_marker = findCurrentMarker(scene);
    QVERIFY(first_marker != nullptr);
    QCOMPARE(first_marker->sceneBoundingRect().center(), testCellCenter("A1B1"));

    scene.setCurrentCell("A1B2");

    QCOMPARE(scene.items().size(), item_count_after_first_update);
    QCOMPARE(findCurrentMarker(scene), first_marker);
    QCOMPARE(first_marker->sceneBoundingRect().center(), testCellCenter("A1B2"));
    QVERIFY(first_marker->isVisible());
}

void GridSceneTests::hidesCurrentMarkerForNonGridWaypoint() {
    TestableGridScene scene;
    scene.setCurrentCell(QStringLiteral("A8B4"));
    auto *marker = findCurrentMarker(scene);
    QVERIFY(marker != nullptr);
    QVERIFY(marker->isVisible());

    scene.setCurrentCell(QStringLiteral("touchdown"));

    QCOMPARE(findCurrentMarker(scene), marker);
    QVERIFY(!marker->isVisible());
}

void GridSceneTests::drawsSeparateDescentStartAndTouchdownMarkers() {
    TestableGridScene scene;
    scene.setLandingTarget("A8B3", 260.0, 190.0, true);

    auto *descent_start = findItemByRole(scene, "descent_start_marker");
    auto *touchdown = findItemByRole(scene, "touchdown_marker");
    auto *corridor = qgraphicsitem_cast<QGraphicsLineItem *>(
        findItemByRole(scene, "landing_corridor"));
    QVERIFY(descent_start != nullptr);
    QVERIFY(touchdown != nullptr);
    QVERIFY(corridor != nullptr);
    QVERIFY(scene.items().contains(findSimpleText(scene, QStringLiteral("下降起点"))));
    QVERIFY(scene.items().contains(findSimpleText(scene, QStringLiteral("降落终点"))));

    const QPointF expected_touchdown(
        ((260.0 - 25.0) / 50.0) * 52.0,
        ((190.0 - 25.0) / 50.0) * 52.0);
    QCOMPARE(descent_start->sceneBoundingRect().center(), testCellCenter("A8B3"));
    QCOMPARE(touchdown->sceneBoundingRect().center(), expected_touchdown);
    QCOMPARE(corridor->line().p1(), testCellCenter("A8B3"));
    QCOMPARE(corridor->line().p2(), expected_touchdown);

    const int initial_item_count = scene.items().size();
    scene.setLandingTarget("A7B2", 210.0, 140.0, true);

    QCOMPARE(scene.items().size(), initial_item_count);
    QCOMPARE(countItemsByRole(scene, QStringLiteral("descent_start_marker")), 1);
    QCOMPARE(countItemsByRole(scene, QStringLiteral("touchdown_marker")), 1);
    QCOMPARE(countItemsByRole(scene, QStringLiteral("landing_corridor")), 1);
    QCOMPARE(countSimpleText(scene, QStringLiteral("下降起点")), 1);
    QCOMPARE(countSimpleText(scene, QStringLiteral("降落终点")), 1);
    QCOMPARE(
        findItemByRole(scene, QStringLiteral("descent_start_marker"))->sceneBoundingRect().center(),
        testCellCenter("A7B2"));
}

void GridSceneTests::zeroTouchdownCoordinatesRemainValid_data() {
    QTest::addColumn<double>("touchdown_x_cm");
    QTest::addColumn<double>("touchdown_y_cm");

    QTest::newRow("origin") << 0.0 << 0.0;
    QTest::newRow("zero-x") << 0.0 << 190.0;
    QTest::newRow("zero-y") << 260.0 << 0.0;
}

void GridSceneTests::zeroTouchdownCoordinatesRemainValid() {
    QFETCH(double, touchdown_x_cm);
    QFETCH(double, touchdown_y_cm);

    TestableGridScene scene;
    scene.setLandingTarget("A9B1", touchdown_x_cm, touchdown_y_cm, true);

    auto *touchdown = findItemByRole(scene, "touchdown_marker");
    QVERIFY(touchdown != nullptr);
    QCOMPARE(touchdown->sceneBoundingRect().center(),
             QPointF(((touchdown_x_cm - 25.0) / 50.0) * 52.0,
                     ((touchdown_y_cm - 25.0) / 50.0) * 52.0));
}

void GridSceneTests::clearsLandingItemsWhenDisabled() {
    TestableGridScene scene;
    scene.setLandingTarget("A8B3", 260.0, 190.0, true);
    scene.setLandingTarget({}, 0.0, 0.0, false);

    QVERIFY(findItemByRole(scene, "descent_start_marker") == nullptr);
    QVERIFY(findItemByRole(scene, "touchdown_marker") == nullptr);
    QVERIFY(findItemByRole(scene, "landing_corridor") == nullptr);
    QVERIFY(findSimpleText(scene, QStringLiteral("下降起点")) == nullptr);
    QVERIFY(findSimpleText(scene, QStringLiteral("降落终点")) == nullptr);
}

QTEST_MAIN(GridSceneTests)
#include "test_grid_scene.moc"
