#include <QtTest/QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "h_problem/storage/h_detection_repository.h"

class DetectionRepositoryTests : public QObject {
    Q_OBJECT

private slots:
    void storesAndAggregatesDetections();
    void returnsDuplicateForRepeatedTrackedDetection();
    void storesRepeatedLegacyEmptyTrackDetections();
    void upgradesLegacySchemaWithoutLosingDetections();
    void separatesRecordsAcrossApplicationSessions();
};

void DetectionRepositoryTests::storesAndAggregatesDetections() {
    DetectionRepository repository(":memory:");
    QVERIFY(repository.open());

    QCOMPARE(
        repository.storeDetection("mission-1", "track-1", "A2B1", "elephant", 1, 1000),
        DetectionRepository::StoreResult::Stored);
    QCOMPARE(
        repository.storeDetection("mission-1", "track-2", "A3B1", "monkey", 2, 2000),
        DetectionRepository::StoreResult::Stored);

    const auto totals = repository.summarizeByAnimal();
    QCOMPARE(totals.value("elephant"), 1);
    QCOMPARE(totals.value("monkey"), 2);
}

void DetectionRepositoryTests::returnsDuplicateForRepeatedTrackedDetection() {
    DetectionRepository repository(":memory:");
    QVERIFY(repository.open());

    QCOMPARE(
        repository.storeDetection("mission-1", "track-1", "A2B1", "elephant", 1, 1000),
        DetectionRepository::StoreResult::Stored);
    QCOMPARE(
        repository.storeDetection("mission-1", "track-1", "A2B1", "elephant", 1, 1000),
        DetectionRepository::StoreResult::Duplicate);

    const auto totals = repository.summarizeByAnimal();
    QCOMPARE(totals.value("elephant"), 1);
}

void DetectionRepositoryTests::storesRepeatedLegacyEmptyTrackDetections() {
    DetectionRepository repository(":memory:");
    QVERIFY(repository.open());

    QCOMPARE(
        repository.storeDetection("mission-1", "", "A2B1", "elephant", 1, 1000),
        DetectionRepository::StoreResult::Stored);
    QCOMPARE(
        repository.storeDetection("mission-1", "", "A2B1", "elephant", 1, 1000),
        DetectionRepository::StoreResult::Stored);

    const auto totals = repository.summarizeByAnimal();
    QCOMPARE(totals.value("elephant"), 2);
}

void DetectionRepositoryTests::upgradesLegacySchemaWithoutLosingDetections() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath("detections.sqlite");
    const QString legacy_connection_name = "legacy_detection_repository_test";
    {
        QSqlDatabase legacy_database = QSqlDatabase::addDatabase("QSQLITE", legacy_connection_name);
        legacy_database.setDatabaseName(database_path);
        QVERIFY(legacy_database.open());

        QSqlQuery query(legacy_database);
        QVERIFY(query.exec(
            "CREATE TABLE detections ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "cell_code TEXT NOT NULL,"
            "animal_name TEXT NOT NULL,"
            "count INTEGER NOT NULL,"
            "timestamp_ms INTEGER NOT NULL)"));
        QVERIFY(query.exec(
            "INSERT INTO detections (cell_code, animal_name, count, timestamp_ms) "
            "VALUES ('A2B1', 'elephant', 1, 1000)"));
        legacy_database.close();
    }
    QSqlDatabase::removeDatabase(legacy_connection_name);

    DetectionRepository repository(database_path);
    QVERIFY(repository.open());
    QCOMPARE(
        repository.storeDetection("mission-1", "track-1", "A3B1", "tiger", 1, 2000),
        DetectionRepository::StoreResult::Stored);

    const auto totals = repository.summarizeByAnimal();
    QCOMPARE(totals.value("elephant"), 0);
    QCOMPARE(totals.value("tiger"), 1);
}

void DetectionRepositoryTests::separatesRecordsAcrossApplicationSessions() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath("detections.sqlite");
    {
        DetectionRepository first_session(database_path);
        QVERIFY(first_session.open());
        QCOMPARE(
            first_session.storeDetection("mission-1", "track-1", "A2B1", "tiger", 1, 1000),
            DetectionRepository::StoreResult::Stored);
        QCOMPARE(first_session.summarizeByAnimal().value("tiger"), 1);
    }
    {
        DetectionRepository second_session(database_path);
        QVERIFY(second_session.open());
        QVERIFY(second_session.summarizeByAnimal().isEmpty());
        QCOMPARE(
            second_session.storeDetection("mission-1", "track-1", "A3B2", "tiger", 1, 2000),
            DetectionRepository::StoreResult::Stored);
        QCOMPARE(second_session.summarizeByAnimal().value("tiger"), 1);
    }
}

QTEST_MAIN(DetectionRepositoryTests)
#include "test_detection_repository.moc"
