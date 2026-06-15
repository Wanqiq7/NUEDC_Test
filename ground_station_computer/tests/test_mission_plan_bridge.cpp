#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "h_problem/mission/h_route_planner_bridge.h"

namespace {

QByteArray makeValidPlannerOutput() {
    QJsonObject plan;
    plan["message_type"] = "config";
    plan["case_id"] = "case-123";
    plan["start_cell"] = "A1B1";
    plan["no_fly_cells"] = QJsonArray{"A2B2", "A3B3"};
    plan["route"] = QJsonArray{"A1B1", "A1B2", "A1B3"};
    plan["terminal_cell"] = "A1B3";
    plan["landing_enabled"] = true;
    plan["descent_angle_deg"] = 5.5;
    plan["takeoff_anchor_x_cm"] = 120.0;
    plan["takeoff_anchor_y_cm"] = 130.0;
    return QJsonDocument(plan).toJson();
}

QByteArray makeJsonWithoutRoute() {
    QJsonObject plan;
    plan["case_id"] = "case-123";
    plan["start_cell"] = "A1B1";
    plan["no_fly_cells"] = QJsonArray{"A2B2", "A3B3"};
    plan["terminal_cell"] = "A1B3";
    plan["landing_enabled"] = true;
    return QJsonDocument(plan).toJson();
}

QByteArray makeJsonWithEmptyRoute() {
    QJsonObject plan;
    plan["case_id"] = "case-123";
    plan["start_cell"] = "A1B1";
    plan["no_fly_cells"] = QJsonArray{"A2B2"};
    plan["route"] = QJsonArray{};
    plan["terminal_cell"] = "A1B1";
    plan["landing_enabled"] = false;
    return QJsonDocument(plan).toJson();
}

QByteArray makeJsonWithEmptyNoFlyCells() {
    QJsonObject plan;
    plan["message_type"] = "config";
    plan["case_id"] = "case-123";
    plan["start_cell"] = "A1B1";
    plan["no_fly_cells"] = QJsonArray{};
    plan["route"] = QJsonArray{"A1B1", "A1B2"};
    plan["terminal_cell"] = "A1B2";
    plan["landing_enabled"] = false;
    return QJsonDocument(plan).toJson();
}

QByteArray makeJsonWithInvalidNoFlyCells() {
    QJsonObject plan;
    plan["case_id"] = "case-123";
    plan["start_cell"] = "A1B1";
    plan["no_fly_cells"] = "bad";
    plan["route"] = QJsonArray{"A1B1", "A1B2"};
    plan["terminal_cell"] = "A1B2";
    plan["landing_enabled"] = false;
    return QJsonDocument(plan).toJson();
}

}

class MissionPlanBridgeTests : public QObject {
    Q_OBJECT

private slots:
    void parsesValidJson();
    void rejectsMissingRoute();
    void rejectsEmptyRoute();
    void acceptsEmptyNoFlyCells();
    void rejectsInvalidNoFlyCellsType();
};

void MissionPlanBridgeTests::parsesValidJson() {
    const MissionPlanResult result = MissionPlanBridge::parsePlannerOutput(makeValidPlannerOutput());
    QVERIFY(result.ok);
    QCOMPARE(result.plan.case_id, QString("case-123"));
    QCOMPARE(result.plan.start_cell, QString("A1B1"));
    const QStringList expectedNoFly{"A2B2", "A3B3"};
    QCOMPARE(result.plan.no_fly_cells, expectedNoFly);
    const QStringList expectedRoute{"A1B1", "A1B2", "A1B3"};
    QCOMPARE(result.plan.route, expectedRoute);
    QCOMPARE(result.plan.terminal_cell, QString("A1B3"));
    QCOMPARE(result.plan.landing_enabled, true);
    QVERIFY(result.plan.descent_angle_deg.has_value());
    QCOMPARE(result.plan.descent_angle_deg.value(), 5.5);
    QVERIFY(result.plan.takeoff_anchor_x_cm.has_value());
    QCOMPARE(result.plan.takeoff_anchor_x_cm.value(), 120.0);
    QVERIFY(result.plan.takeoff_anchor_y_cm.has_value());
    QCOMPARE(result.plan.takeoff_anchor_y_cm.value(), 130.0);
}

void MissionPlanBridgeTests::rejectsMissingRoute() {
    const MissionPlanResult result = MissionPlanBridge::parsePlannerOutput(makeJsonWithoutRoute());
    QVERIFY(!result.ok);
    QVERIFY(result.error_message.contains("route"));
}

void MissionPlanBridgeTests::rejectsEmptyRoute() {
    const MissionPlanResult result = MissionPlanBridge::parsePlannerOutput(makeJsonWithEmptyRoute());
    QVERIFY(!result.ok);
    QVERIFY(result.error_message.contains("route"));
}

void MissionPlanBridgeTests::acceptsEmptyNoFlyCells() {
    const MissionPlanResult result = MissionPlanBridge::parsePlannerOutput(makeJsonWithEmptyNoFlyCells());
    QVERIFY(result.ok);
    QVERIFY(result.plan.no_fly_cells.isEmpty());
}

void MissionPlanBridgeTests::rejectsInvalidNoFlyCellsType() {
    const MissionPlanResult result = MissionPlanBridge::parsePlannerOutput(makeJsonWithInvalidNoFlyCells());
    QVERIFY(!result.ok);
    QVERIFY(result.error_message.contains("no_fly_cells"));
}

QTEST_MAIN(MissionPlanBridgeTests)
#include "test_mission_plan_bridge.moc"
