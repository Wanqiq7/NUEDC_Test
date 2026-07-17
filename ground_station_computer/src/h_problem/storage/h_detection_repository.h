#pragma once

#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class DetectionRepository {
public:
    enum class StoreResult {
        Stored,
        Duplicate,
        Failed,
    };

    explicit DetectionRepository(const QString &database_path);
    ~DetectionRepository();

    bool open();
    StoreResult storeDetection(
        const QString &task_id,
        const QString &track_id,
        const QString &cell_code,
        const QString &animal_name,
        int count,
        qint64 timestamp_ms);
    QMap<QString, int> summarizeByAnimal() const;
    QStringList animalNames() const;
    QMap<QString, int> locationsForAnimal(const QString &animal_name) const;

private:
    QString connection_name_;
    QString session_id_;
    QString database_path_;
    QSqlDatabase database_;
};
