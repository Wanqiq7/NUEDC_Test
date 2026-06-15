#include <QtTest/QtTest>

#include <QFile>
#include <QRegularExpression>

class ArchitectureBoundaryTests : public QObject {
    Q_OBJECT

private slots:
    void mainWindowDoesNotIncludeProblemSpecificHeaders();
    void mainWindowUsesReliableCommandClientForControlCommands();
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

QTEST_MAIN(ArchitectureBoundaryTests)
#include "test_architecture_boundaries.moc"
