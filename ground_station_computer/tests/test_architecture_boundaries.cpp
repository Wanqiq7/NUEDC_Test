#include <QtTest/QtTest>

#include <QFile>
#include <QRegularExpression>

class ArchitectureBoundaryTests : public QObject {
    Q_OBJECT

private slots:
    void mainWindowDoesNotIncludeProblemSpecificHeaders();
    void mainWindowUsesReliableCommandClientForControlCommands();
    void mainWindowUsesConfiguredTaskAdapterFactory();
    void frameworkCommunicationDoesNotIncludeProblemSpecificHeaders();
    void subscriberWorkerPublishesGenericTaskSignals();
    void competitionTaskAdapterExposesGenericProtocolHandlers();
    void mainWindowDoesNotOwnProblemSpecificBusinessPanels();
    void documentsCanonicalTaskPlanStorage();
};

void ArchitectureBoundaryTests::mainWindowDoesNotIncludeProblemSpecificHeaders() {
    QFile file("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(file.readAll());

    const QRegularExpression forbidden_include(R"(#include\s+["<][^">]*h_problem[^">]*[">])");
    QVERIFY2(
        !forbidden_include.match(source).hasMatch(),
        "MainWindow must depend on CompetitionTaskAdapter factory instead of h_problem headers");
}

void ArchitectureBoundaryTests::mainWindowUsesConfiguredTaskAdapterFactory() {
    QFile source("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source_text = QString::fromUtf8(source.readAll());
    QVERIFY(source_text.contains("createConfiguredCompetitionTaskAdapter"));
    QVERIFY(source_text.contains("createDefaultCompetitionTaskAdapter"));
}
void ArchitectureBoundaryTests::mainWindowUsesReliableCommandClientForControlCommands() {
    QFile header("ground_station_computer/src/app/main_window.h");
    QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString header_source = QString::fromUtf8(header.readAll());
    QVERIFY(header_source.contains("ReliableCommandClient"));

    QFile source("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source_text = QString::fromUtf8(source.readAll());
    QVERIFY(source_text.contains("sendReliable"));
    QVERIFY(source_text.contains("reliable_command_client_->ping"));
}

void ArchitectureBoundaryTests::frameworkCommunicationDoesNotIncludeProblemSpecificHeaders() {
    const QStringList paths = {
        "ground_station_computer/src/framework/communication/zmq_subscriber_worker.h",
        "ground_station_computer/src/framework/communication/zmq_subscriber_worker.cpp",
    };

    for (const QString &path : paths) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(
            !source.contains("h_problem"),
            qPrintable(QString("%1 must not depend on h_problem").arg(path)));
    }
}

void ArchitectureBoundaryTests::subscriberWorkerPublishesGenericTaskSignals() {
    QFile header("ground_station_computer/src/framework/communication/zmq_subscriber_worker.h");
    QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(header.readAll());

    QVERIFY(source.contains("taskPlanReceived"));
    QVERIFY(source.contains("taskEventReceived"));
    QVERIFY(source.contains("taskSummaryReceived"));
    QVERIFY(!source.contains("gridConfigReceived"));
    QVERIFY(!source.contains("telemetryReceived"));
    QVERIFY(!source.contains("detectionReceived"));
    QVERIFY(!source.contains("summaryReceived(QMap"));
}

void ArchitectureBoundaryTests::competitionTaskAdapterExposesGenericProtocolHandlers() {
    QFile header("ground_station_computer/src/framework/task/competition_task_adapter.h");
    QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(header.readAll());

    QVERIFY(source.contains("handleTaskPlan"));
    QVERIFY(source.contains("handleTaskEvent"));
    QVERIFY(source.contains("handleTaskSummary"));
    QVERIFY(!source.contains("TaskGridConfig"));
    QVERIFY(!source.contains("handleTelemetry"));
    QVERIFY(!source.contains("handleDetection"));
    QVERIFY(!source.contains("QMap<QString, int>"));
}

void ArchitectureBoundaryTests::mainWindowDoesNotOwnProblemSpecificBusinessPanels() {
    QFile header("ground_station_computer/src/app/main_window.h");
    QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString header_source = QString::fromUtf8(header.readAll());
    QVERIFY(!header_source.contains("handleGridConfig"));
    QVERIFY(!header_source.contains("handleTelemetry"));
    QVERIFY(!header_source.contains("handleDetection"));
    QVERIFY(!header_source.contains("QTableWidget"));
    QVERIFY(!header_source.contains("QListWidget"));

    QFile source("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source_text = QString::fromUtf8(source.readAll());
    QVERIFY(!source_text.contains("实时检测记录"));
    QVERIFY(!source_text.contains("统计汇总"));
    QVERIFY(!source_text.contains("动物"));
    QVERIFY(!source_text.contains("H 题混合联调地面站"));
}

void ArchitectureBoundaryTests::documentsCanonicalTaskPlanStorage() {
    QFile file("docs/framework_architecture.md");
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(file.readAll());
    QVERIFY(contents.contains("运行时任务计划持久化统一使用 `competition::TaskPlan`"));
}

QTEST_MAIN(ArchitectureBoundaryTests)
#include "test_architecture_boundaries.moc"
