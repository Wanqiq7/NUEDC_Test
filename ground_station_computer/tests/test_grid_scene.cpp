#include <QtTest/QtTest>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>

#include "h_problem/rules/h_grid_mapper.h"
#include "h_problem/ui/h_grid_scene.h"

namespace {
constexpr int kTestRows = 7;
constexpr qreal kTestCellSize = 52.0;
constexpr int kCurrentMarkerDataKey = 1001;
constexpr const char *kCurrentMarkerDataValue = "current_marker";

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

QTEST_MAIN(GridSceneTests)
#include "test_grid_scene.moc"
