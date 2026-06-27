#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "h_problem/storage/h_mission_plan_store.h"

class MissionPlanStoreTests : public QObject {
    Q_OBJECT

private slots:
    void savesCompatibleMissionPlanJson();
    void writesNullForMissingOptionalFields();
};

void MissionPlanStoreTests::savesCompatibleMissionPlanJson() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output_path = QDir(dir.path()).filePath("runtime/active_mission_plan.json");

    MissionPlanData plan;
    plan.case_id = "case-123";
    plan.start_cell = "A1B1";
    plan.no_fly_cells = {"A2B2", "A3B2"};
    plan.route = {"A1B1", "A1B2"};
    plan.terminal_cell = "A1B2";
    plan.landing_enabled = true;
    plan.descent_angle_deg = 45.0;
    plan.takeoff_anchor_x_cm = 120.0;
    plan.takeoff_anchor_y_cm = 130.0;

    QString error;
    QVERIFY2(MissionPlanStore(output_path).save(plan, &error), qPrintable(error));

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("message_type").toString(), QString("config"));
    QCOMPARE(object.value("case_id").toString(), plan.case_id);
    QCOMPARE(object.value("start_cell").toString(), plan.start_cell);
    QCOMPARE(object.value("terminal_cell").toString(), plan.terminal_cell);
    QCOMPARE(object.value("landing_enabled").toBool(), true);
    QCOMPARE(object.value("descent_angle_deg").toDouble(), 45.0);
    QCOMPARE(object.value("takeoff_anchor_x_cm").toDouble(), 120.0);
    QCOMPARE(object.value("takeoff_anchor_y_cm").toDouble(), 130.0);
    QCOMPARE(object.value("no_fly_cells").toArray().at(1).toString(), QString("A3B2"));
    QCOMPARE(object.value("route").toArray().at(1).toString(), QString("A1B2"));
}

void MissionPlanStoreTests::writesNullForMissingOptionalFields() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output_path = QDir(dir.path()).filePath("active_mission_plan.json");

    MissionPlanData plan;
    plan.case_id = "case-123";
    plan.start_cell = "A1B1";
    plan.route = {"A1B1"};
    plan.terminal_cell = "A1B1";

    QVERIFY(MissionPlanStore(output_path).save(plan));

    QFile file(output_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QVERIFY(object.value("descent_angle_deg").isNull());
    QVERIFY(object.value("takeoff_anchor_x_cm").isNull());
    QVERIFY(object.value("takeoff_anchor_y_cm").isNull());
}

QTEST_MAIN(MissionPlanStoreTests)
#include "test_mission_plan_store.moc"
