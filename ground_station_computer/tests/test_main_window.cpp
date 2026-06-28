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
#include <QMetaObject>
#include <QPushButton>

#include <QCoreApplication>

#include <memory>

#include "h_problem_core/protocol/envelope_builder.h"
#include "framework/task/competition_task_adapter.h"
#include "h_problem/rules/h_grid_mapper.h"
#include "app/main_window.h"

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
}

class MainWindowTests : public QObject {
    Q_OBJECT

private slots:
    void adapterFactoryListsDefaultProblemAdapter();
    void configuredAdapterUsesEnvironmentVariableAndReportsUnknownIds();
    void shellUsesCompetitionTaskAdapterBoundary();
    void defaultAdapterConsumesCommandAckRuntimeState();
    void executionControlsExistAndAreDisabledInTestMode();
    void taskMapExpandsInsideLargeShellWindow();
    void manualNoFlyFlowPersistsPlan();
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
    adapter.reset(createConfiguredCompetitionTaskAdapter(&error));
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

    adapter->applyCommandAck(CommandSendResult{true, "start accepted", "case-001", true, true, 88});
    const MissionRuntimeInputs inputs = adapter->missionRuntimeInputs();

    QCOMPARE(inputs.acknowledged_task_id, QString("case-001"));
    QVERIFY(inputs.acknowledged_mission_loaded);
    QVERIFY(inputs.mission_running);
    QCOMPARE(inputs.last_accepted_sequence, 88ULL);
}
void MainWindowTests::executionControlsExistAndAreDisabledInTestMode() {
    MainWindow window(nullptr, false);
    window.show();
    QTest::qWait(50);

    auto *execute_button = window.findChild<QPushButton *>("ExecuteMissionButton");
    auto *stop_button = window.findChild<QPushButton *>("StopMissionButton");
    auto *airborne_status = window.findChild<QLabel *>("AirborneStatusLabel");
    auto *planning_button = window.findChild<QPushButton *>("PlanningButton");

    QVERIFY(execute_button != nullptr);
    QVERIFY(stop_button != nullptr);
    QVERIFY(airborne_status != nullptr);
    QVERIFY(planning_button != nullptr);
    QVERIFY(planning_button->isEnabled());
    QVERIFY(!execute_button->isEnabled());
    QVERIFY(!stop_button->isEnabled());
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

QTEST_MAIN(MainWindowTests)
#include "test_main_window.moc"
