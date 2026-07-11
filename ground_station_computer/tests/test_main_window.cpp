#include <QtTest/QtTest>
#include <QByteArray>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QTableWidget>
#include <QUuid>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>

#include "h_problem_core/protocol/envelope_builder.h"
#include "competition_core/protocol/envelope_codec.h"
#include "h_problem/rules/h_grid_mapper.h"
#include "h_problem/ui/h_problem_page.h"
#define private public
#include "app/main_window.h"
#undef private

#include <thread>

#include <zmq.hpp>

namespace {
constexpr int kTestRows = 7;
constexpr qreal kTestCellSize = 52.0;

QString findRepositoryRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("shared/cases/sample_case.json"))
            && dir.exists(QStringLiteral("ground_station_computer/src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::current().absolutePath();
}

QPointF testCellCenter(const QString &code) {
    const QPoint point = GridMapper::toPoint(code);
    const qreal scene_y_index = (kTestRows - 1) - point.y();
    return QPointF((point.x() * kTestCellSize) + (kTestCellSize / 2.0),
                   (scene_y_index * kTestCellSize) + (kTestCellSize / 2.0));
}

class PingServer {
public:
    explicit PingServer(int reply_count = 1)
        : socket_(context_, zmq::socket_type::rep),
          reply_count_(reply_count) {
        socket_.set(zmq::sockopt::linger, 0);
        socket_.set(zmq::sockopt::rcvtimeo, 10000);
        socket_.bind("tcp://127.0.0.1:*");
        endpoint_ = QString::fromStdString(socket_.get(zmq::sockopt::last_endpoint));
        worker_ = std::thread([this] {
            for (int reply_index = 0; reply_index < reply_count_; ++reply_index) {
                zmq::message_t request;
                if (!socket_.recv(request, zmq::recv_flags::none)) {
                    return;
                }
                const QByteArray reply = competition::buildAckBytes(true, "pong");
                if (!socket_.send(zmq::buffer(reply.constData(), reply.size()), zmq::send_flags::none)) {
                    return;
                }
            }
        });
    }

    ~PingServer() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    QString port() const { return endpoint_.section(':', -1); }

private:
    zmq::context_t context_{1};
    zmq::socket_t socket_;
    QString endpoint_;
    std::thread worker_;
    int reply_count_ = 1;
};
}

class MainWindowTests : public QObject {
    Q_OBJECT

private slots:
    void adapterFactoryListsDefaultProblemAdapter();
    void configuredAdapterUsesEnvironmentVariableAndReportsUnknownIds();
    void shellUsesCompetitionTaskAdapterBoundary();
    void defaultAdapterConsumesCommandAckRuntimeState();
    void defaultAdapterConsumesStateOnlyVisionAck();
    void executionControlsExistAndAreDisabledInTestMode();
    void taskMapExpandsInsideLargeShellWindow();
    void manualNoFlyFlowPersistsPlan();
    void targetUpdateOnlyUpdatesLiveTargetStatus();
    void duplicateDetectionDoesNotDuplicateUiTotals();
    void unknownEventDoesNotFallThroughToTelemetry();
    void telemetryCannotEnableCommandControlsWhenCommandLinkIsOffline();
    void staleCommandHealthDisablesVisionControlsDespiteTelemetry();
    void probeActionRecoversExpiredCommandHealth();
};

void MainWindowTests::adapterFactoryListsDefaultProblemAdapter() {
    const QVector<CompetitionTaskAdapterDescriptor> descriptors = availableCompetitionTaskAdapters();
    QVERIFY(!descriptors.isEmpty());
    QCOMPARE(descriptors.first().adapter_id, QString("h_problem"));
    QVERIFY(!descriptors.first().display_name.isEmpty());
    QVERIFY(static_cast<bool>(descriptors.first().create));

    std::unique_ptr<CompetitionTaskAdapter> adapter(createCompetitionTaskAdapter("h_problem"));
    QVERIFY(adapter != nullptr);
    QCOMPARE(adapter->initialPlanningButtonText(), QString("设置禁飞区"));
}

void MainWindowTests::configuredAdapterUsesEnvironmentVariableAndReportsUnknownIds() {
    qputenv("NUEDC_TASK_ADAPTER", QByteArray("h_problem"));
    QString error;
    std::unique_ptr<CompetitionTaskAdapter> adapter(createConfiguredCompetitionTaskAdapter(&error));
    QVERIFY(adapter != nullptr);
    QVERIFY(error.isEmpty());

    qputenv("NUEDC_TASK_ADAPTER", QByteArray("missing_problem"));
    adapter = createConfiguredCompetitionTaskAdapter(&error);
    QVERIFY(adapter == nullptr);
    QVERIFY(error.contains("unknown task adapter"));
    QVERIFY(error.contains("h_problem"));
    qunsetenv("NUEDC_TASK_ADAPTER");
}

void MainWindowTests::shellUsesCompetitionTaskAdapterBoundary() {
    std::unique_ptr<CompetitionTaskAdapter> adapter(createDefaultCompetitionTaskAdapter());
    QVERIFY(adapter != nullptr);
    QCOMPARE(adapter->initialPlanningButtonText(), QString("设置禁飞区"));
}


void MainWindowTests::defaultAdapterConsumesCommandAckRuntimeState() {
    std::unique_ptr<CompetitionTaskAdapter> adapter(createDefaultCompetitionTaskAdapter());
    QVERIFY(adapter != nullptr);

    adapter->applyCommandAck(CommandSendResult{true, "start accepted", "case-001", true, true, 88, true});
    const MissionRuntimeInputs inputs = adapter->missionRuntimeInputs();

    QCOMPARE(inputs.acknowledged_task_id, QString("case-001"));
    QVERIFY(inputs.acknowledged_mission_loaded);
    QVERIFY(inputs.mission_running);
    QCOMPARE(inputs.last_accepted_sequence, 88ULL);
    QVERIFY(inputs.vision_armed);
}

void MainWindowTests::defaultAdapterConsumesStateOnlyVisionAck() {
    HProblemTaskAdapter adapter;
    QWidget parent;
    std::unique_ptr<QWidget> task_view(adapter.createTaskView(&parent));
    QVERIFY(task_view != nullptr);
    adapter.loadInitialPreview();

    adapter.applyCommandAck(CommandSendResult{true, "vision armed", {}, false, false, 0, true});

    QVERIFY(adapter.missionRuntimeInputs().vision_armed);
    bool vision_state_displayed = false;
    for (QLabel *label : task_view->findChildren<QLabel *>()) {
        if (label->text().startsWith("任务:") && label->text().contains("已武装")) {
            vision_state_displayed = true;
        }
    }
    QVERIFY(vision_state_displayed);
}
void MainWindowTests::executionControlsExistAndAreDisabledInTestMode() {
    MainWindow window(nullptr, false);
    window.show();
    QTest::qWait(50);

    auto *execute_button = window.findChild<QPushButton *>("ExecuteMissionButton");
    auto *stop_button = window.findChild<QPushButton *>("StopMissionButton");
    auto *arm_vision_button = window.findChild<QPushButton *>("ArmVisionButton");
    auto *reset_vision_button = window.findChild<QPushButton *>("ResetVisionButton");
    auto *airborne_status = window.findChild<QLabel *>("AirborneStatusLabel");
    auto *planning_button = window.findChild<QPushButton *>("PlanningButton");

    QVERIFY(execute_button != nullptr);
    QVERIFY(stop_button != nullptr);
    QVERIFY(arm_vision_button != nullptr);
    QVERIFY(reset_vision_button != nullptr);
    QVERIFY(airborne_status != nullptr);
    QVERIFY(planning_button != nullptr);
    QVERIFY(planning_button->isEnabled());
    QVERIFY(!execute_button->isEnabled());
    QVERIFY(!stop_button->isEnabled());
    QVERIFY(!arm_vision_button->isEnabled());
    QVERIFY(!reset_vision_button->isEnabled());
    QVERIFY(airborne_status->text().contains("测试模式"));
}

void MainWindowTests::taskMapExpandsInsideLargeShellWindow() {
    MainWindow window(nullptr, false);
    window.resize(1840, 1040);
    window.show();
    QTest::qWait(50);

    auto *view = window.findChild<QGraphicsView *>("TaskView");
    QVERIFY(view != nullptr);
    QVERIFY(view->viewport()->width() > 900);

    const QRectF mapped_scene = view->mapToScene(view->viewport()->rect()).boundingRect();
    QVERIFY(mapped_scene.width() < 700.0);
}

void MainWindowTests::manualNoFlyFlowPersistsPlan() {
    const QString repo_root = findRepositoryRoot();
    const QString plan_path = QDir(repo_root).filePath("runtime/active_mission_plan.json");
    QFile::remove(plan_path);

    const QStringList selected_cells = {"A1B2", "A2B2", "A3B2"};

    {
        MainWindow window(nullptr, false);
        window.show();
        QTest::qWait(50);

        auto *planning_button = window.findChild<QPushButton *>("PlanningButton");
        QVERIFY(planning_button != nullptr);
        planning_button->click();
        QCoreApplication::processEvents();

        auto *view = window.findChild<QGraphicsView *>();
        QVERIFY(view != nullptr);
        for (const QString &cell : selected_cells) {
            const QPoint click_pos = view->mapFromScene(testCellCenter(cell));
            QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, click_pos);
            QCoreApplication::processEvents();
        }

        planning_button->click();
        QCoreApplication::processEvents();

        QTRY_VERIFY(QFileInfo::exists(plan_path));

        QFile file(plan_path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonObject object = document.object();
        const QJsonArray array = object.value("no_fly_cells").toArray();
        QStringList persisted_cells;
        for (const QJsonValue &entry : array) {
            persisted_cells.append(entry.toString());
        }
        QCOMPARE(persisted_cells, selected_cells);
    }

    QVERIFY(QFile::remove(plan_path));
}

void MainWindowTests::targetUpdateOnlyUpdatesLiveTargetStatus() {
    HProblemTaskAdapter adapter;
    QWidget parent;
    std::unique_ptr<QWidget> task_view(adapter.createTaskView(&parent));
    QVERIFY(task_view != nullptr);

    auto *detection_list = task_view->findChild<QListWidget *>();
    auto *summary_table = task_view->findChild<QTableWidget *>();
    auto *target_status = task_view->findChild<QLabel *>("TargetStatusLabel");
    QVERIFY(detection_list != nullptr);
    QVERIFY(summary_table != nullptr);
    QVERIFY(target_status != nullptr);
    const int initial_detection_count = detection_list->count();
    const int initial_summary_rows = summary_table->rowCount();

    adapter.handleTaskEvent(
        competition::TaskEvent{
            "mission-target-update",
            "target_update",
            17,
            "A3B2",
            R"({"track_id":"track-7","cell":"A3B2","animal":"falcon","score":0.91,"target_offset_x_px":14,"target_offset_y_px":-8,"visited_cells":0})"},
        1000);

    QCOMPARE(detection_list->count(), initial_detection_count);
    QCOMPARE(summary_table->rowCount(), initial_summary_rows);
    QVERIFY(target_status->text().contains("track-7"));
    QVERIFY(target_status->text().contains("A3B2"));
    QVERIFY(target_status->text().contains("falcon"));
    QVERIFY(target_status->text().contains("0.91"));
    QVERIFY(target_status->text().contains("14"));
    QVERIFY(target_status->text().contains("-8"));
}

void MainWindowTests::duplicateDetectionDoesNotDuplicateUiTotals() {
    HProblemTaskAdapter adapter;
    QWidget parent;
    std::unique_ptr<QWidget> task_view(adapter.createTaskView(&parent));
    QVERIFY(task_view != nullptr);

    auto *detection_list = task_view->findChild<QListWidget *>();
    auto *summary_table = task_view->findChild<QTableWidget *>();
    QVERIFY(detection_list != nullptr);
    QVERIFY(summary_table != nullptr);
    const int initial_detection_count = detection_list->count();
    const QString task_id = QString("mission-ui-dedup-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString animal_name = QString("ui-dedup-falcon-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const competition::TaskEvent event{
        task_id,
        "detection",
        1,
        "A2B1",
        QString(R"({"track_id":"track-7","cell":"A2B1","animal":"%1","count":3})").arg(animal_name)};

    adapter.handleTaskEvent(event, 2000);
    adapter.handleTaskEvent(event, 2000);

    QCOMPARE(detection_list->count(), initial_detection_count + 1);
    int matching_rows = 0;
    for (int row = 0; row < summary_table->rowCount(); ++row) {
        if (summary_table->item(row, 0)->text() == animal_name) {
            ++matching_rows;
            QCOMPARE(summary_table->item(row, 1)->text(), QString("3"));
        }
    }
    QCOMPARE(matching_rows, 1);
}

void MainWindowTests::unknownEventDoesNotFallThroughToTelemetry() {
    HProblemTaskAdapter adapter;
    QString status_text;
    adapter.setStatusTextCallback([&status_text](const QString &text) { status_text = text; });

    adapter.handleTaskEvent(
        competition::TaskEvent{
            "mission-unknown-event",
            "unsupported_event",
            3,
            "A4B1",
            R"({"current_cell":"A4B1","visited_cells":5})"},
        3000);

    QVERIFY(status_text.isEmpty());
}

void MainWindowTests::telemetryCannotEnableCommandControlsWhenCommandLinkIsOffline() {
    qputenv("NUEDC_AIRBORNE_HOST", "127.0.0.1");
    qputenv("NUEDC_COMMAND_PORT", "1");

    MainWindow window(nullptr, true);
    window.show();
    QTest::qWait(50);

    auto *execute_button = window.findChild<QPushButton *>("ExecuteMissionButton");
    auto *stop_button = window.findChild<QPushButton *>("StopMissionButton");
    auto *arm_vision_button = window.findChild<QPushButton *>("ArmVisionButton");
    auto *reset_vision_button = window.findChild<QPushButton *>("ResetVisionButton");
    QVERIFY(execute_button != nullptr);
    QVERIFY(stop_button != nullptr);
    QVERIFY(arm_vision_button != nullptr);
    QVERIFY(reset_vision_button != nullptr);

    window.task_adapter_->applyCommandAck(CommandSendResult{
        true,
        "mission loaded",
        window.task_adapter_->activeTaskId(),
        true,
        false,
        101,
        false,
    });

    QVERIFY(!execute_button->isEnabled());
    QVERIFY(!stop_button->isEnabled());
    QVERIFY(!arm_vision_button->isEnabled());
    QVERIFY(!reset_vision_button->isEnabled());

    window.handleTaskEvent(
        competition::TaskEvent{
            window.task_adapter_->activeTaskId(),
            "telemetry",
            1,
            "A1B1",
            R"({\"current_cell\":\"A1B1\",\"visited_cells\":1})"},
        1000);

    QVERIFY(!execute_button->isEnabled());
    QVERIFY(!stop_button->isEnabled());
    QVERIFY(!arm_vision_button->isEnabled());
    QVERIFY(!reset_vision_button->isEnabled());

    qunsetenv("NUEDC_AIRBORNE_HOST");
    qunsetenv("NUEDC_COMMAND_PORT");
}

void MainWindowTests::staleCommandHealthDisablesVisionControlsDespiteTelemetry() {
    PingServer server;
    qputenv("NUEDC_AIRBORNE_HOST", "127.0.0.1");
    qputenv("NUEDC_COMMAND_PORT", server.port().toUtf8());

    MainWindow window(nullptr, true);
    window.show();
    QTest::qWait(50);

    auto *arm_vision_button = window.findChild<QPushButton *>("ArmVisionButton");
    auto *reset_vision_button = window.findChild<QPushButton *>("ResetVisionButton");
    QVERIFY(arm_vision_button != nullptr);
    QVERIFY(reset_vision_button != nullptr);

    window.task_adapter_->applyCommandAck(CommandSendResult{
        true,
        "mission loaded",
        window.task_adapter_->activeTaskId(),
        true,
        false,
        102,
        false,
    });
    QVERIFY(arm_vision_button->isEnabled());
    QVERIFY(reset_vision_button->isEnabled());

    window.last_successful_command_reply_ms_ = QDateTime::currentMSecsSinceEpoch() - 6000;
    window.refreshExecutionControls();
    window.handleTaskEvent(
        competition::TaskEvent{
            window.task_adapter_->activeTaskId(),
            "telemetry",
            2,
            "A1B1",
            R"({\"current_cell\":\"A1B1\",\"visited_cells\":2})"},
        2000);

    QVERIFY(!arm_vision_button->isEnabled());
    QVERIFY(!reset_vision_button->isEnabled());

    qunsetenv("NUEDC_AIRBORNE_HOST");
    qunsetenv("NUEDC_COMMAND_PORT");
}

void MainWindowTests::probeActionRecoversExpiredCommandHealth() {
    PingServer server(2);
    qputenv("NUEDC_AIRBORNE_HOST", "127.0.0.1");
    qputenv("NUEDC_COMMAND_PORT", server.port().toUtf8());

    MainWindow window(nullptr, true);
    window.show();
    QTest::qWait(50);

    auto *arm_vision_button = window.findChild<QPushButton *>("ArmVisionButton");
    auto *reset_vision_button = window.findChild<QPushButton *>("ResetVisionButton");
    auto *probe_button = window.findChild<QPushButton *>("ProbeAirborneLinkButton");
    QVERIFY(arm_vision_button != nullptr);
    QVERIFY(reset_vision_button != nullptr);
    QVERIFY(probe_button != nullptr);
    QVERIFY(probe_button->isEnabled());

    window.task_adapter_->applyCommandAck(CommandSendResult{
        true,
        "mission loaded",
        window.task_adapter_->activeTaskId(),
        true,
        false,
        103,
        false,
    });
    QVERIFY(arm_vision_button->isEnabled());
    QVERIFY(reset_vision_button->isEnabled());

    QTest::qWait(5500);
    QVERIFY(!arm_vision_button->isEnabled());
    QVERIFY(!reset_vision_button->isEnabled());

    probe_button->click();
    QCoreApplication::processEvents();

    QVERIFY(arm_vision_button->isEnabled());
    QVERIFY(reset_vision_button->isEnabled());

    qunsetenv("NUEDC_AIRBORNE_HOST");
    qunsetenv("NUEDC_COMMAND_PORT");
}

QTEST_MAIN(MainWindowTests)
#include "test_main_window.moc"
