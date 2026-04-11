#include <QtTest/QtTest>

#include "route_validator.h"

class RouteValidatorTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsJumpAcrossObstacle();
    void acceptsAdjacentRoute();
    void acceptsClosedRouteReturningToStart();
};

void RouteValidatorTests::rejectsJumpAcrossObstacle() {
    const QStringList route = {"A3B3", "A7B3"};
    QString error;
    QVERIFY(!RouteValidator::validateRoute(route, {"A4B3", "A5B3", "A6B3"}, &error));
    QVERIFY(error.contains("illegal jump"));
}

void RouteValidatorTests::acceptsAdjacentRoute() {
    const QStringList route = {"A3B3", "A3B4", "A4B4"};
    QString error;
    QVERIFY(RouteValidator::validateRoute(route, {"A4B3", "A5B3", "A6B3"}, &error));
    QVERIFY(error.isEmpty());
}

void RouteValidatorTests::acceptsClosedRouteReturningToStart() {
    const QStringList route = {"A9B7", "A8B7", "A8B6", "A9B6", "A9B7"};
    QString error;
    QVERIFY(RouteValidator::validateClosedRoute(route, {"A4B3", "A5B3", "A6B3"}, "A9B7", &error));
    QVERIFY(error.isEmpty());
}

QTEST_MAIN(RouteValidatorTests)
#include "test_route_validator.moc"
