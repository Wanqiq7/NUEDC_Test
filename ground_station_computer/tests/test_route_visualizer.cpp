#include <QtTest/QtTest>

#include "h_problem/ui/h_route_visualizer.h"

class RouteVisualizerTests : public QObject {
    Q_OBJECT

private slots:
    void marksRepeatedUndirectedEdgesWithUnifiedHighlightMetadata();
    void keepsNonRepeatedEdgesAsPrimaryRoute();
};

void RouteVisualizerTests::marksRepeatedUndirectedEdgesWithUnifiedHighlightMetadata() {
    const QStringList route = {"A1B1", "A2B1", "A1B1", "A2B1"};
    const auto visuals = RouteVisualizer::buildSegmentVisuals(route);

    QCOMPARE(visuals.size(), 3);
    QVERIFY(visuals[0].is_repeated);
    QVERIFY(visuals[1].is_repeated);
    QVERIFY(visuals[2].is_repeated);
    QCOMPARE(visuals[0].repeat_count, 3);
    QCOMPARE(visuals[1].repeat_count, 3);
    QCOMPARE(visuals[2].repeat_count, 3);
    QCOMPARE(visuals[0].badge_text, QString("3x"));
    QCOMPARE(visuals[1].badge_text, QString("3x"));
    QCOMPARE(visuals[2].badge_text, QString("3x"));
    QCOMPARE(visuals[0].color, visuals[1].color);
    QCOMPARE(visuals[1].color, visuals[2].color);
}

void RouteVisualizerTests::keepsNonRepeatedEdgesAsPrimaryRoute() {
    const QStringList route = {"A1B1", "A2B1", "A3B1"};
    const auto visuals = RouteVisualizer::buildSegmentVisuals(route);

    QCOMPARE(visuals.size(), 2);
    QVERIFY(!visuals[0].is_repeated);
    QVERIFY(!visuals[1].is_repeated);
    QCOMPARE(visuals[0].repeat_count, 1);
    QCOMPARE(visuals[1].repeat_count, 1);
    QVERIFY(visuals[0].badge_text.isEmpty());
    QVERIFY(visuals[1].badge_text.isEmpty());
    QCOMPARE(visuals[0].color, QColor(47, 111, 237));
    QCOMPARE(visuals[1].color, QColor(47, 111, 237));
}

QTEST_MAIN(RouteVisualizerTests)
#include "test_route_visualizer.moc"
