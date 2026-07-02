#include <QtTest/QtTest>

#include "framework/config/repository_paths.h"

#include <QDir>
#include <QFileInfo>

class RepositoryPathsTests : public QObject {
    Q_OBJECT

private slots:
    void rootIsStableAndAbsolute();
    void rootContainsMarkerDirectories();
    void resolveJoinsRelativeToRoot();
    void resolveResolvesKnownCasesDirectory();
};

void RepositoryPathsTests::rootIsStableAndAbsolute() {
    const QString first = RepositoryPaths::root();
    const QString second = RepositoryPaths::root();
    QVERIFY(!first.isEmpty());
    QCOMPARE(first, second);  // 进程内缓存，稳定。
    QVERIFY(QDir::isAbsolutePath(first));
}

void RepositoryPathsTests::rootContainsMarkerDirectories() {
    // ④ 的关键：以标志目录存在性判定根，而非某个具体样例文件名。
    const QDir root(RepositoryPaths::root());
    QVERIFY2(root.exists(QStringLiteral("shared/cases")),
             "repository root must contain shared/cases directory");
    QVERIFY2(root.exists(QStringLiteral("ground_station_computer/src")),
             "repository root must contain ground_station_computer/src directory");
}

void RepositoryPathsTests::resolveJoinsRelativeToRoot() {
    const QString resolved = RepositoryPaths::resolve(QStringLiteral("runtime/x.db"));
    QCOMPARE(resolved, QDir(RepositoryPaths::root()).filePath(QStringLiteral("runtime/x.db")));
}

void RepositoryPathsTests::resolveResolvesKnownCasesDirectory() {
    const QString cases_dir = RepositoryPaths::resolve(QStringLiteral("shared/cases"));
    QVERIFY(QFileInfo(cases_dir).isDir());
}

QTEST_MAIN(RepositoryPathsTests)
#include "test_repository_paths.moc"
