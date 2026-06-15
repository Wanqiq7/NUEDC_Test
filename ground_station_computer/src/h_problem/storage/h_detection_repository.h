#pragma once

#include <QMap>
#include <QSqlDatabase>
#include <QString>

class DetectionRepository {
public:
    explicit DetectionRepository(const QString &database_path);
    ~DetectionRepository();

    bool open();
    bool storeDetection(const QString &cell_code, const QString &animal_name, int count, qint64 timestamp_ms);
    QMap<QString, int> summarizeByAnimal() const;

private:
    QString connection_name_;
    QString database_path_;
    QSqlDatabase database_;
};
