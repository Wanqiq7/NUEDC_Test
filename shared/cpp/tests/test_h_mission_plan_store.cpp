#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "h_problem_core/mission/mission_plan_store.h"

namespace {

hcore::MissionPlan makePlan() {
    hcore::MissionPlan plan;
    plan.case_id = "demo";
    plan.start_cell = "A9B1";
    plan.no_fly_cells = {"A1B2", "A2B2", "A3B2"};
    plan.route = {"A9B1", "A8B1", "A7B1"};
    plan.terminal_cell = "A7B1";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 450.0;
    plan.takeoff_anchor_y_cm = 350.0;
    return plan;
}

}

class HMissionPlanStoreTests : public QObject {
    Q_OBJECT

private slots:
    void storesReceivedMissionPlan();
    void rejectsRouteIntersectingNoFlyZone();
    void readsStoredMissionPlan();
    void readsLegacyMissionPlanJsonDuringMigration();
};

void HMissionPlanStoreTests::storesReceivedMissionPlan() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const QString output_path = QDir(dir.path()).filePath("runtime/active_mission_plan.json");
    QVERIFY2(hcore::storeMissionPlan(makePlan(), output_path, &error), qPrintable(error));

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("message_type").toString(), QString("task_plan"));
    QCOMPARE(object.value("task_id").toString(), QString("demo"));
    QCOMPARE(object.value("task_type").toString(), QString("h_problem"));
    QCOMPARE(object.value("start_waypoint_id").toString(), QString("A9B1"));
    QCOMPARE(object.value("terminal_waypoint_id").toString(), QString("A7B1"));
    QCOMPARE(object.value("waypoints").toArray().at(2).toObject().value("id").toString(), QString("A7B1"));

    const QJsonObject metadata = QJsonDocument::fromJson(object.value("metadata_json").toString().toUtf8()).object();
    QCOMPARE(metadata.value("case_id").toString(), QString("demo"));
    QCOMPARE(metadata.value("no_fly_cells").toArray().at(1).toString(), QString("A2B2"));
    QCOMPARE(metadata.value("landing_enabled").toBool(), true);
}

void HMissionPlanStoreTests::rejectsRouteIntersectingNoFlyZone() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    hcore::MissionPlan plan = makePlan();
    plan.route = {"A9B1", "A1B2"};

    QString error;
    const QString output_path = QDir(dir.path()).filePath("active_mission_plan.json");
    QVERIFY(!hcore::storeMissionPlan(plan, output_path, &error));
    QVERIFY(error.contains("route intersects no_fly_cells"));
    QVERIFY(!QFile::exists(output_path));
}

void HMissionPlanStoreTests::readsStoredMissionPlan() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const QString output_path = QDir(dir.path()).filePath("active_mission_plan.json");
    QVERIFY2(hcore::storeMissionPlan(makePlan(), output_path, &error), qPrintable(error));

    const auto loaded = hcore::loadMissionPlan(output_path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->case_id, QString("demo"));
    QCOMPARE(loaded->no_fly_cells, QStringList({"A1B2", "A2B2", "A3B2"}));
    QCOMPARE(loaded->route, QStringList({"A9B1", "A8B1", "A7B1"}));
}

void HMissionPlanStoreTests::readsLegacyMissionPlanJsonDuringMigration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = QDir(dir.path()).filePath("legacy_mission_plan.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QJsonDocument(hcore::missionPlanToJson(makePlan())).toJson(QJsonDocument::Compact));
    file.close();

    QString error;
    const auto loaded = hcore::loadMissionPlan(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->case_id, QString("demo"));
    QCOMPARE(loaded->route, QStringList({"A9B1", "A8B1", "A7B1"}));
    QCOMPARE(loaded->landing_enabled, true);
}

QTEST_MAIN(HMissionPlanStoreTests)
#include "test_h_mission_plan_store.moc"
