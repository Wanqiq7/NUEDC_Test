#include <QtTest/QtTest>

#include "h_problem/mission/h_mission_controller.h"
#include "h_problem/mission/h_mission_view_sink.h"

#include "competition_core/task/models.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

// 记录式 mock sink：断言控制器对视图发出的调用，无需真实 Qt Widgets。
class RecordingSink : public HMissionViewSink {
public:
    void setCaseLabel(const QString &text) override { case_label = text; }
    void setMissionLabel(const QString &text) override { mission_label = text; }
    void showRoute(
        const QStringList &no_fly_cells,
        const QStringList &route,
        const QString &start_cell,
        const QString &terminal_cell,
        double takeoff_anchor_x_cm,
        double takeoff_anchor_y_cm,
        bool landing_enabled) override {
        ++show_route_calls;
        last_no_fly = no_fly_cells;
        last_route = route;
        last_start = start_cell;
        last_terminal = terminal_cell;
        last_anchor_x = takeoff_anchor_x_cm;
        last_anchor_y = takeoff_anchor_y_cm;
        last_landing = landing_enabled;
    }
    void enterNoFlyEditMode() override { ++enter_edit_calls; }
    void setCandidateCells(const QStringList &cells) override { last_candidates = cells; }
    void setCurrentCell(const QString &cell_code) override { last_current_cell = cell_code; }
    void appendDetection(const QString &text) override { detections.append(text); }
    void setSummaryTotals(const QMap<QString, int> &totals) override { last_totals = totals; }

    QString case_label;
    QString mission_label;
    int show_route_calls = 0;
    int enter_edit_calls = 0;
    QStringList last_no_fly;
    QStringList last_route;
    QString last_start;
    QString last_terminal;
    double last_anchor_x = 0.0;
    double last_anchor_y = 0.0;
    bool last_landing = false;
    QStringList last_candidates;
    QString last_current_cell;
    QStringList detections;
    QMap<QString, int> last_totals;
};

competition::TaskEvent makeTelemetryEvent(const QString &cell, int step, int visited) {
    competition::TaskEvent event;
    event.event_type = "telemetry";
    event.sequence_index = static_cast<quint32>(step);
    event.waypoint_id = cell;
    QJsonObject payload;
    payload["current_cell"] = cell;
    payload["visited_cells"] = visited;
    event.payload_json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return event;
}

competition::TaskEvent makeDetectionEvent(const QString &cell, const QString &animal, int count) {
    competition::TaskEvent event;
    event.event_type = "detection";
    event.waypoint_id = cell;
    QJsonObject payload;
    payload["cell_code"] = cell;
    payload["animal_name"] = animal;
    payload["count"] = count;
    event.payload_json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return event;
}

competition::TaskSummary makeSummary(const QMap<QString, int> &totals, int visited) {
    competition::TaskSummary summary;
    summary.visited_waypoints = static_cast<quint32>(visited);
    QJsonObject totals_object;
    for (auto it = totals.begin(); it != totals.end(); ++it) {
        totals_object[it.key()] = it.value();
    }
    QJsonObject payload;
    payload["totals"] = totals_object;
    summary.payload_json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return summary;
}

} // namespace

class HMissionControllerTests : public QObject {
    Q_OBJECT

private slots:
    void loadInitialPreviewShowsRouteAndStatus();
    void telemetryUpdatesCurrentCellAndRunning();
    void detectionAppendsAndAccumulatesTotals();
    void summaryStopsRunningAndReplacesTotals();
    void planningButtonEntersNoFlyEditMode();
    void cellSelectionAccumulatesCandidatesAndValidates();
};

void HMissionControllerTests::loadInitialPreviewShowsRouteAndStatus() {
    RecordingSink sink;
    QString last_status;
    HMissionController controller(
        &sink,
        [&](const QString &text) { last_status = text; },
        [&](const QString &) {},
        [&]() {});

    controller.loadInitialPreview();

    // 默认样例应成功生成并展示航线。
    QVERIFY(sink.show_route_calls >= 1);
    QVERIFY(!controller.activeTaskId().isEmpty());
    QVERIFY(last_status.contains("默认航线已加载"));
    QVERIFY(!sink.case_label.isEmpty());
}

void HMissionControllerTests::telemetryUpdatesCurrentCellAndRunning() {
    RecordingSink sink;
    int runtime_changes = 0;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() { ++runtime_changes; });

    controller.handleTaskEvent(makeTelemetryEvent("C3", 5, 9), 1000);

    QCOMPARE(sink.last_current_cell, QStringLiteral("C3"));
    QVERIFY(controller.missionRunning());
    QVERIFY(runtime_changes >= 1);
}

void HMissionControllerTests::detectionAppendsAndAccumulatesTotals() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});

    const QMap<QString, int> baseline = controller.detectionTotals();
    const int baseline_tiger = baseline.value("tiger", 0);

    controller.handleTaskEvent(makeDetectionEvent("B2", "tiger", 2), 2000);
    controller.handleTaskEvent(makeDetectionEvent("B3", "tiger", 3), 3000);

    QCOMPARE(sink.detections.size(), 2);
    QCOMPARE(controller.detectionTotals().value("tiger"), baseline_tiger + 5);
    QCOMPARE(sink.last_totals.value("tiger"), baseline_tiger + 5);
}

void HMissionControllerTests::summaryStopsRunningAndReplacesTotals() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});

    controller.handleTaskEvent(makeTelemetryEvent("C3", 5, 9), 1000);
    QVERIFY(controller.missionRunning());

    controller.handleTaskSummary(makeSummary({{"deer", 4}}, 12));

    QVERIFY(!controller.missionRunning());
    QCOMPARE(sink.last_totals.value("deer"), 4);
}

void HMissionControllerTests::planningButtonEntersNoFlyEditMode() {
    RecordingSink sink;
    QString last_status;
    HMissionController controller(
        &sink,
        [&](const QString &text) { last_status = text; },
        [&](const QString &) {},
        [&]() {});

    controller.loadInitialPreview();
    const int edit_before = sink.enter_edit_calls;

    controller.handlePlanningButtonClicked();

    QCOMPARE(sink.enter_edit_calls, edit_before + 1);
    QVERIFY(last_status.contains("禁飞格"));
}

void HMissionControllerTests::cellSelectionAccumulatesCandidatesAndValidates() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});

    controller.loadInitialPreview();
    controller.handlePlanningButtonClicked();  // 进入选择模式

    controller.handleGridSceneCellClicked("D2");
    controller.handleGridSceneCellClicked("D3");
    QCOMPARE(sink.last_candidates.size(), 2);

    // 再次点击同一格应取消选择。
    controller.handleGridSceneCellClicked("D3");
    QCOMPARE(sink.last_candidates.size(), 1);
}

QTEST_MAIN(HMissionControllerTests)
#include "test_h_mission_controller.moc"
