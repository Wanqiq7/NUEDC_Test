#include <QtTest/QtTest>

#include "detection_repository.h"

class DetectionRepositoryTests : public QObject {
    Q_OBJECT

private slots:
    void storesAndAggregatesDetections();
};

void DetectionRepositoryTests::storesAndAggregatesDetections() {
    DetectionRepository repository(":memory:");
    QVERIFY(repository.open());

    QVERIFY(repository.storeDetection("A2B1", "elephant", 1, 1000));
    QVERIFY(repository.storeDetection("A3B1", "monkey", 2, 2000));

    const auto totals = repository.summarizeByAnimal();
    QCOMPARE(totals.value("elephant"), 1);
    QCOMPARE(totals.value("monkey"), 2);
}

QTEST_MAIN(DetectionRepositoryTests)
#include "test_detection_repository.moc"
