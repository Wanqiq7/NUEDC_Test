#include <QtTest/QtTest>

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>

#include "competition_core/storage/json_codec.h"

class JsonCodecTests : public QObject {
    Q_OBJECT

private slots:
    void writesAndReadsJsonObject();
    void requiredStringReportsMissingField();
};

void JsonCodecTests::writesAndReadsJsonObject() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject object;
    object["task_id"] = "demo";

    QString error;
    const QString path = QDir(dir.path()).filePath("nested/task.json");
    QVERIFY2(competition::writeJsonObject(object, path, &error), qPrintable(error));

    const auto loaded = competition::readJsonObject(path, "task plan", &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->value("task_id").toString(), QString("demo"));
}

void JsonCodecTests::requiredStringReportsMissingField() {
    QString output;
    QString error;

    QVERIFY(!competition::requiredString(QJsonObject(), "task_id", &output, &error));
    QCOMPARE(error, QString("missing task_id"));
}

QTEST_MAIN(JsonCodecTests)
#include "test_json_codec.moc"
