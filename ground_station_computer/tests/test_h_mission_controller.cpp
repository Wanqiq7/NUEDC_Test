#include <QtTest/QtTest>

#include "h_problem/mission/h_mission_controller.h"
#include "h_problem/mission/h_mission_view_sink.h"
#include "h_problem/mission/h_protocol_adapter.h"
#include "h_problem/mission/h_route_planner_bridge.h"

#include "competition_core/task/models.h"
#include "competition_core/mission/task_plan_store.h"
#include "framework/config/repository_paths.h"
#include "framework/communication/reliable_command_client.h"
#include "messages.pb.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

// 记录式 mock sink：断言控制器对视图发出的调用，无需真实 Qt Widgets。
class RecordingSink : public HMissionViewSink {
public:
    void setCaseLabel(const QString &text) override { case_label = text; }
    void setMissionLabel(const QString &text) override { mission_label = text; }
    void setTargetStatus(const QString &text) override { target_status = text; }
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
    QString target_status;
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
    static int track_index = 0;
    static const QString task_id = QStringLiteral("controller-test-task-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    competition::TaskEvent event;
    event.task_id = task_id;
    event.event_type = "detection";
    event.waypoint_id = cell;
    QJsonObject payload;
    payload["track_id"] = QString("controller-track-%1").arg(++track_index);
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

competition::TaskEvent makeTargetUpdateEvent() {
    competition::TaskEvent event;
    event.event_type = "target_update";
    event.waypoint_id = "A2B2";
    event.payload_json = R"({"track_id":"tracked-animal","cell":"A2B2","animal":"deer","score":0.91,"target_offset_x_px":12,"target_offset_y_px":-4,"visited_cells":0})";
    return event;
}

class RecordingCommandTransport final : public CommandTransport {
public:
    CommandSendResult sendEnvelope(const Envelope &envelope) const override {
        sent_envelopes_.append(envelope);
        return CommandSendResult{
            true,
            "vision reset",
            QString::fromStdString(envelope.control_command().task_id()),
            true,
            false,
            envelope.sequence(),
            false,
        };
    }

    QVector<CommandType> sentControlTypes() const {
        QVector<CommandType> types;
        for (const Envelope &envelope : sent_envelopes_) {
            if (envelope.payload_case() == Envelope::kControlCommand) {
                types.append(envelope.control_command().type());
            }
        }
        return types;
    }

private:
    mutable QVector<Envelope> sent_envelopes_;
};

competition::TaskPlan makeCanonicalTaskPlan() {
    HRoutePlanner planner;
    const competition::TaskPlanningResult result = planner.generatePlan("shared/cases/sample_case.json", {});
    Q_ASSERT(result.ok);
    return result.plan;
}

QJsonObject metadataObject(const competition::TaskPlan &plan) {
    const QJsonDocument document = QJsonDocument::fromJson(plan.metadata_json.toUtf8());
    Q_ASSERT(document.isObject());
    return document.object();
}

void replaceMetadata(competition::TaskPlan *plan, const QJsonObject &metadata) {
    plan->metadata_json = QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
}

} // namespace

class HMissionControllerTests : public QObject {
    Q_OBJECT

private slots:
    void loadInitialPreviewShowsRouteAndStatus();
    void loadInitialPreviewShowsPlanningOptimalityToOperator();
    void telemetryUpdatesCurrentCellAndRunning();
    void detectionAppendsAndAccumulatesTotals();
    void summaryStopsRunningAndReplacesTotals();
    void missionCompletionDisarmsVisionAndClearsTarget();
    void missionStopDisarmsVisionAndClearsTarget();
    void taskPlanReplacementDisarmsVisionAndClearsTarget();
    void regeneratedTaskPlanDisarmsVisionAndClearsTarget();
    void planningButtonEntersNoFlyEditMode();
    void cellSelectionAccumulatesCandidatesAndValidates();
    void rejectsTaskPlanWithIncompleteMetadata();
    void rejectsMalformedTaskPlanWithoutChangingDisplayedRoute();
    void rejectsMalformedTaskPlanStructure();
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

    QString error;
    const auto persisted = competition::loadTaskPlan(
        RepositoryPaths::resolve("runtime/active_mission_plan.json"), &error);
    QVERIFY2(persisted.has_value(), qPrintable(error));
    QCOMPARE(persisted->task_type, QString("h_problem"));
    QVERIFY(!persisted->metadata_json.isEmpty());
}

void HMissionControllerTests::loadInitialPreviewShowsPlanningOptimalityToOperator() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});

    controller.loadInitialPreview();

    QVERIFY(sink.mission_label.contains("预计:"));
    QVERIFY(sink.mission_label.contains("规划:"));
    QVERIFY(sink.last_anchor_x != 0.0);
    QVERIFY(sink.last_anchor_y != 0.0);
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

void HMissionControllerTests::missionCompletionDisarmsVisionAndClearsTarget() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});
    controller.handleTaskPlan(makeCanonicalTaskPlan());

    RecordingCommandTransport transport;
    controller.setCommandTransport(&transport);
    controller.applyCommandAck(CommandSendResult{true, "vision armed", {}, false, false, 0, true});
    controller.handleTaskEvent(makeTargetUpdateEvent(), 1000);
    QVERIFY(controller.missionRuntimeInputs().vision_armed);
    QVERIFY(sink.target_status.contains("tracked-animal"));

    controller.handleTaskSummary(makeSummary({{"deer", 4}}, 12));

    QCOMPARE(transport.sentControlTypes(), QVector<CommandType>{COMMAND_TYPE_RESET_TARGETING});
    QVERIFY(!controller.missionRuntimeInputs().vision_armed);
    QCOMPARE(sink.target_status, QStringLiteral("目标: 等待跟踪"));
}

void HMissionControllerTests::missionStopDisarmsVisionAndClearsTarget() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});
    controller.handleTaskPlan(makeCanonicalTaskPlan());

    RecordingCommandTransport transport;
    controller.setCommandTransport(&transport);
    controller.applyCommandAck(CommandSendResult{true, "vision armed", {}, false, false, 0, true});
    controller.handleTaskEvent(makeTargetUpdateEvent(), 1000);

    controller.markControlCommandStopped();

    QCOMPARE(transport.sentControlTypes(), QVector<CommandType>{COMMAND_TYPE_RESET_TARGETING});
    QVERIFY(!controller.missionRuntimeInputs().vision_armed);
    QCOMPARE(sink.target_status, QStringLiteral("目标: 等待跟踪"));
}

void HMissionControllerTests::taskPlanReplacementDisarmsVisionAndClearsTarget() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});
    controller.handleTaskPlan(makeCanonicalTaskPlan());

    RecordingCommandTransport transport;
    controller.setCommandTransport(&transport);
    controller.applyCommandAck(CommandSendResult{true, "vision armed", {}, false, false, 0, true});
    controller.handleTaskEvent(makeTargetUpdateEvent(), 1000);

    controller.handleTaskPlan(makeCanonicalTaskPlan());

    QCOMPARE(transport.sentControlTypes(), QVector<CommandType>{COMMAND_TYPE_RESET_TARGETING});
    QVERIFY(!controller.missionRuntimeInputs().vision_armed);
    QCOMPARE(sink.target_status, QStringLiteral("目标: 等待跟踪"));
}

void HMissionControllerTests::regeneratedTaskPlanDisarmsVisionAndClearsTarget() {
    RecordingSink sink;
    HMissionController controller(
        &sink,
        [&](const QString &) {},
        [&](const QString &) {},
        [&]() {});
    controller.handleTaskPlan(makeCanonicalTaskPlan());

    RecordingCommandTransport transport;
    controller.setCommandTransport(&transport);
    controller.applyCommandAck(CommandSendResult{true, "vision armed", {}, false, false, 0, true});
    controller.handleTaskEvent(makeTargetUpdateEvent(), 1000);

    controller.handlePlanningButtonClicked();
    controller.handleGridSceneCellClicked("A1B2");
    controller.handleGridSceneCellClicked("A2B2");
    controller.handleGridSceneCellClicked("A3B2");
    controller.handlePlanningButtonClicked();

    QCOMPARE(transport.sentControlTypes(), QVector<CommandType>{COMMAND_TYPE_RESET_TARGETING});
    QVERIFY(!controller.missionRuntimeInputs().vision_armed);
    QCOMPARE(sink.target_status, QStringLiteral("目标: 等待跟踪"));
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

void HMissionControllerTests::rejectsTaskPlanWithIncompleteMetadata() {
    RecordingSink sink;
    QString status;
    HMissionController controller(
        &sink,
        [&](const QString &text) { status = text; },
        [&](const QString &) {},
        [&]() {});

    competition::TaskPlan plan;
    plan.task_id = "incomplete";
    plan.task_type = "h_problem";
    plan.start_waypoint_id = "A1B1";
    plan.terminal_waypoint_id = "A1B1";
    plan.waypoints = {{"A1B1", 0}};
    plan.metadata_json = R"({"case_id":"incomplete"})";

    controller.handleTaskPlan(plan);

    QVERIFY(status.contains("metadata"));
    QCOMPARE(sink.show_route_calls, 0);
}

void HMissionControllerTests::rejectsMalformedTaskPlanWithoutChangingDisplayedRoute() {
    RecordingSink sink;
    QString status;
    HMissionController controller(
        &sink,
        [&](const QString &text) { status = text; },
        [&](const QString &) {},
        [&]() {});

    controller.loadInitialPreview();
    const int show_route_calls_before = sink.show_route_calls;
    const QStringList route_before = sink.last_route;
    const QString start_before = sink.last_start;
    const QString terminal_before = sink.last_terminal;

    competition::TaskPlan malformed = makeCanonicalTaskPlan();
    QJsonObject metadata = metadataObject(malformed);
    metadata["touchdown_x_cm"] = QJsonValue::Null;
    replaceMetadata(&malformed, metadata);

    controller.handleTaskPlan(malformed);

    QCOMPARE(sink.show_route_calls, show_route_calls_before);
    QCOMPARE(sink.last_route, route_before);
    QCOMPARE(sink.last_start, start_before);
    QCOMPARE(sink.last_terminal, terminal_before);
    QVERIFY(status.startsWith("错误:"));
}

void HMissionControllerTests::rejectsMalformedTaskPlanStructure() {
    const competition::TaskPlan canonical = makeCanonicalTaskPlan();

    const auto assertRejected = [](const competition::TaskPlan &plan) {
        HGridConfigData decoded;
        QString error;
        QVERIFY2(!HProtocolAdapter::decodeTaskPlan(plan, &decoded, &error), qPrintable(error));
        QVERIFY(!error.isEmpty());
    };

    competition::TaskPlan non_contiguous_sequence = canonical;
    non_contiguous_sequence.waypoints[1].sequence_index = 7;
    assertRejected(non_contiguous_sequence);

    competition::TaskPlan diagonal_route = canonical;
    diagonal_route.waypoints[1].id = "A2B2";
    assertRejected(diagonal_route);

    competition::TaskPlan out_of_grid_route = canonical;
    out_of_grid_route.waypoints[1].id = "A10B1";
    assertRejected(out_of_grid_route);

    competition::TaskPlan no_fly_route = canonical;
    QJsonObject no_fly_metadata = metadataObject(no_fly_route);
    no_fly_metadata["no_fly_cells"] = QJsonArray{"A1B1"};
    replaceMetadata(&no_fly_route, no_fly_metadata);
    assertRejected(no_fly_route);

    competition::TaskPlan incomplete_coverage = canonical;
    incomplete_coverage.waypoints.removeLast();
    incomplete_coverage.terminal_waypoint_id = incomplete_coverage.waypoints.last().id;
    QJsonObject coverage_metadata = metadataObject(incomplete_coverage);
    coverage_metadata["terminal_cell"] = incomplete_coverage.terminal_waypoint_id;
    replaceMetadata(&incomplete_coverage, coverage_metadata);
    assertRejected(incomplete_coverage);

    competition::TaskPlan invalid_landing = canonical;
    QJsonObject landing_metadata = metadataObject(invalid_landing);
    landing_metadata["touchdown_x_cm"] = landing_metadata.value("touchdown_x_cm").toDouble() + 10.0;
    replaceMetadata(&invalid_landing, landing_metadata);
    assertRejected(invalid_landing);
}

QTEST_MAIN(HMissionControllerTests)
#include "test_h_mission_controller.moc"
