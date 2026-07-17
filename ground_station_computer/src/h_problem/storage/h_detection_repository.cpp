#include "h_problem/storage/h_detection_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

bool hasColumn(const QSqlDatabase &database, const QString &column_name) {
    QSqlQuery query(database);
    if (!query.exec("PRAGMA table_info(detections)")) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column_name) {
            return true;
        }
    }
    return false;
}

bool addNullableColumnIfMissing(const QSqlDatabase &database, const QString &column_name) {
    if (hasColumn(database, column_name)) {
        return true;
    }

    QSqlQuery query(database);
    return query.exec(QString("ALTER TABLE detections ADD COLUMN %1 TEXT").arg(column_name));
}

} // namespace

DetectionRepository::DetectionRepository(const QString &database_path)
    : connection_name_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      session_id_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      database_path_(database_path) {}

DetectionRepository::~DetectionRepository() {
    const QString connection_name = connection_name_;
    if (database_.isValid()) {
        database_.close();
    }
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
}

bool DetectionRepository::open() {
    if (database_.isValid() && database_.isOpen()) {
        return true;
    }
    database_ = QSqlDatabase::addDatabase("QSQLITE", connection_name_);
    database_.setDatabaseName(database_path_);
    if (!database_.open()) {
        return false;
    }

    QSqlQuery query(database_);
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS detections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "cell_code TEXT NOT NULL,"
        "animal_name TEXT NOT NULL,"
        "count INTEGER NOT NULL,"
        "timestamp_ms INTEGER NOT NULL,"
        "session_id TEXT,"
        "task_id TEXT,"
        "track_id TEXT)")) {
        return false;
    }

    if (!addNullableColumnIfMissing(database_, "session_id")
        || !addNullableColumnIfMissing(database_, "task_id")
        || !addNullableColumnIfMissing(database_, "track_id")) {
        return false;
    }

    if (!query.exec("DROP INDEX IF EXISTS detections_task_track_unique")) {
        return false;
    }
    return query.exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS detections_session_task_track_unique "
        "ON detections(session_id, task_id, track_id) "
        "WHERE session_id IS NOT NULL AND session_id <> '' "
        "AND task_id IS NOT NULL AND task_id <> '' "
        "AND track_id IS NOT NULL AND track_id <> ''");
}

DetectionRepository::StoreResult DetectionRepository::storeDetection(
    const QString &task_id,
    const QString &track_id,
    const QString &cell_code,
    const QString &animal_name,
    int count,
    qint64 timestamp_ms) {
    QSqlQuery query(database_);
    query.prepare(
        "INSERT OR IGNORE INTO detections "
        "(session_id, task_id, track_id, cell_code, animal_name, count, timestamp_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(session_id_);
    query.addBindValue(task_id);
    query.addBindValue(track_id);
    query.addBindValue(cell_code);
    query.addBindValue(animal_name);
    query.addBindValue(count);
    query.addBindValue(timestamp_ms);
    if (!query.exec()) {
        return StoreResult::Failed;
    }
    if (query.numRowsAffected() > 0) {
        return StoreResult::Stored;
    }

    if (task_id.isEmpty() || track_id.isEmpty()) {
        return StoreResult::Failed;
    }

    QSqlQuery duplicate_query(database_);
    duplicate_query.prepare(
        "SELECT 1 FROM detections "
        "WHERE session_id = ? AND task_id = ? AND track_id = ? LIMIT 1");
    duplicate_query.addBindValue(session_id_);
    duplicate_query.addBindValue(task_id);
    duplicate_query.addBindValue(track_id);
    if (!duplicate_query.exec() || !duplicate_query.next()) {
        return StoreResult::Failed;
    }
    return StoreResult::Duplicate;
}

QMap<QString, int> DetectionRepository::summarizeByAnimal() const {
    QMap<QString, int> totals;
    QSqlQuery query(database_);
    query.prepare(
        "SELECT animal_name, SUM(count) FROM detections "
        "WHERE session_id = ? GROUP BY animal_name ORDER BY animal_name");
    query.addBindValue(session_id_);
    query.exec();
    while (query.next()) {
        totals.insert(query.value(0).toString(), query.value(1).toInt());
    }
    return totals;
}

QStringList DetectionRepository::animalNames() const {
    QStringList names;
    QSqlQuery query(database_);
    query.prepare(
        "SELECT DISTINCT animal_name FROM detections "
        "WHERE session_id = ? ORDER BY animal_name");
    query.addBindValue(session_id_);
    query.exec();
    while (query.next()) {
        names.append(query.value(0).toString());
    }
    return names;
}

QMap<QString, int> DetectionRepository::locationsForAnimal(const QString &animal_name) const {
    QMap<QString, int> locations;
    QSqlQuery query(database_);
    query.prepare(
        "SELECT cell_code, SUM(count) FROM detections "
        "WHERE session_id = ? AND animal_name = ? "
        "GROUP BY cell_code ORDER BY cell_code");
    query.addBindValue(session_id_);
    query.addBindValue(animal_name);
    query.exec();
    while (query.next()) {
        locations.insert(query.value(0).toString(), query.value(1).toInt());
    }
    return locations;
}
