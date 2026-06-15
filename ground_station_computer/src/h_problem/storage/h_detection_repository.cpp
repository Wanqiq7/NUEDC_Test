#include "h_problem/storage/h_detection_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

DetectionRepository::DetectionRepository(const QString &database_path)
    : connection_name_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      database_path_(database_path) {}

DetectionRepository::~DetectionRepository() {
    if (database_.isValid()) {
        database_.close();
    }
    QSqlDatabase::removeDatabase(connection_name_);
}

bool DetectionRepository::open() {
    database_ = QSqlDatabase::addDatabase("QSQLITE", connection_name_);
    database_.setDatabaseName(database_path_);
    if (!database_.open()) {
        return false;
    }

    QSqlQuery query(database_);
    return query.exec(
        "CREATE TABLE IF NOT EXISTS detections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "cell_code TEXT NOT NULL,"
        "animal_name TEXT NOT NULL,"
        "count INTEGER NOT NULL,"
        "timestamp_ms INTEGER NOT NULL)");
}

bool DetectionRepository::storeDetection(
    const QString &cell_code,
    const QString &animal_name,
    int count,
    qint64 timestamp_ms) {
    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO detections (cell_code, animal_name, count, timestamp_ms) "
        "VALUES (?, ?, ?, ?)");
    query.addBindValue(cell_code);
    query.addBindValue(animal_name);
    query.addBindValue(count);
    query.addBindValue(timestamp_ms);
    return query.exec();
}

QMap<QString, int> DetectionRepository::summarizeByAnimal() const {
    QMap<QString, int> totals;
    QSqlQuery query(database_);
    query.exec("SELECT animal_name, SUM(count) FROM detections GROUP BY animal_name");
    while (query.next()) {
        totals.insert(query.value(0).toString(), query.value(1).toInt());
    }
    return totals;
}
