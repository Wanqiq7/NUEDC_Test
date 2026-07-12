#include <QtTest/QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"

class HCaseLoaderTests : public QObject {
    Q_OBJECT

private slots:
    void parsesSampleCase();
    void rejectsDeprecatedReturnToStartConfiguration();
    void buildTaskPlanRejectsMissingLandingProfile();
    void buildTaskPlanPropagatesMissionTiming();
    void buildTaskPlanProducesCanonicalPlanWithLandingMetadata();
};

void HCaseLoaderTests::parsesSampleCase() {
    QString error;
    const auto maybe_case = hcore::loadCase("shared/cases/sample_case.json", &error);
    QVERIFY2(maybe_case.has_value(), qPrintable(error));

    const hcore::CaseConfig loaded = maybe_case.value();
    QCOMPARE(loaded.case_id, QString("wildlife-demo"));
    QCOMPARE(loaded.start_cell, QString("A9B1"));
    QCOMPARE(loaded.no_fly_cells, QStringList({"A4B3", "A5B3", "A6B3"}));
    QCOMPARE(loaded.tick_interval_ms, 150);
    QCOMPARE(loaded.animals.size(), 4);
    QVERIFY(loaded.landing.has_value());
    QCOMPARE(loaded.landing->takeoff_anchor_cm.x_cm, 450.0);
    QCOMPARE(loaded.landing->descent_angle_deg, 45.0);
    QCOMPARE(loaded.mission_timing.cruise_speed_cm_per_s, 125.0);
    QCOMPARE(loaded.mission_timing.ascent_speed_cm_per_s, 80.0);
    QCOMPARE(loaded.mission_timing.descent_speed_cm_per_s, 70.0);
    QCOMPARE(loaded.mission_timing.takeoff_fixed_time_s, 2.0);
    QCOMPARE(loaded.mission_timing.landing_fixed_time_s, 3.0);
    QCOMPARE(loaded.mission_timing.per_cell_dwell_time_s, 0.1);
}

void HCaseLoaderTests::rejectsDeprecatedReturnToStartConfiguration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString case_path = dir.filePath("closed_route_case.json");
    QFile file(case_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray json = R"({"case_id":"closed-route","start_cell":"A1B1","return_to_start":true})";
    QCOMPARE(file.write(json), static_cast<qint64>(json.size()));
    file.close();

    QString error;
    const auto loaded = hcore::loadCase(case_path, &error);

    QVERIFY(!loaded.has_value());
    QVERIFY(error.contains("return_to_start"));
}

void HCaseLoaderTests::buildTaskPlanRejectsMissingLandingProfile() {
    hcore::CaseConfig config;
    config.case_id = "missing-landing";
    config.start_cell = "A1B1";

    QString error;
    const auto plan = hcore::buildTaskPlan(config, {}, &error);

    QVERIFY(!plan.has_value());
    QVERIFY(error.contains("landing"));
}

void HCaseLoaderTests::buildTaskPlanPropagatesMissionTiming() {
    QString error;
    const auto maybe_case = hcore::loadCase("shared/cases/sample_case.json", &error);
    QVERIFY2(maybe_case.has_value(), qPrintable(error));

    hcore::CaseConfig invalid_timing_case = maybe_case.value();
    invalid_timing_case.mission_timing.cruise_speed_cm_per_s = 0.0;
    const auto plan = hcore::buildTaskPlan(invalid_timing_case, {}, &error);

    QVERIFY(!plan.has_value());
    QVERIFY(error.contains("mission timing"));
}

void HCaseLoaderTests::buildTaskPlanProducesCanonicalPlanWithLandingMetadata() {
    QString error;
    const auto maybe_case = hcore::loadCase("shared/cases/sample_case.json", &error);
    QVERIFY2(maybe_case.has_value(), qPrintable(error));

    const auto plan = hcore::buildTaskPlan(maybe_case.value(), {}, &error);

    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->task_id, maybe_case->case_id);
    QCOMPARE(plan->task_type, QString("h_problem"));
    QCOMPARE(plan->start_waypoint_id, maybe_case->start_cell);
    QVERIFY(!plan->waypoints.isEmpty());
    QCOMPARE(plan->terminal_waypoint_id, plan->waypoints.last().id);
    const QJsonObject metadata = QJsonDocument::fromJson(plan->metadata_json.toUtf8()).object();
    const QStringList required_metadata{
        "case_id", "start_cell", "no_fly_cells", "terminal_cell", "landing_enabled",
        "descent_angle_deg", "takeoff_anchor_x_cm", "takeoff_anchor_y_cm",
        "touchdown_x_cm", "touchdown_y_cm", "descent_run_cm", "descent_heading_deg",
        "estimated_mission_time_s", "planning_optimality", "planning_warnings",
    };
    for (const QString &field : required_metadata) {
        QVERIFY2(metadata.contains(field), qPrintable(field));
    }
}

QTEST_MAIN(HCaseLoaderTests)
#include "test_h_case_loader.moc"
