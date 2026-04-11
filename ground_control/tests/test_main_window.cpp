#include <QtTest/QtTest>

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

#include "grid_mapper.h"
#include "grid_scene.h"
#include "main_window.h"

namespace {
constexpr int kTestRows = 7;
constexpr qreal kTestCellSize = 52.0;

QString findRepositoryRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("shared")) && dir.exists(QStringLiteral("airborne"))) {
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
    void executionControlsExistAndAreDisabledInTestMode();
    void manualNoFlyFlowPersistsPlan();
};

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

void MainWindowTests::manualNoFlyFlowPersistsPlan() {
    const QString repo_root = findRepositoryRoot();
    const QString plan_path = QDir(repo_root).filePath("runtime/active_mission_plan.json");
    QFile::remove(plan_path);

    QStringList no_fly_cells = {"A4B3", "A5B3", "A6B3"};
    QStringList route = {"A9B1", "A9B2", "A8B2"};
    const QStringList selected_cells = {"A1B2", "A2B2", "A3B2"};

    {
        MainWindow window(nullptr, false);
        window.show();
        QTest::qWait(50);
        QMetaObject::invokeMethod(
            &window,
            "handleGridConfig",
            Qt::DirectConnection,
            Q_ARG(QString, "wildlife-demo"),
            Q_ARG(QString, "A9B1"),
            Q_ARG(QStringList, no_fly_cells),
            Q_ARG(QStringList, route),
            Q_ARG(QString, "A8B2"),
            Q_ARG(bool, true),
            Q_ARG(double, 45.0),
            Q_ARG(double, 450.0),
            Q_ARG(double, 350.0));

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
