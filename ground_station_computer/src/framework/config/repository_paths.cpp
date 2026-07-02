#include "framework/config/repository_paths.h"

#include <QCoreApplication>
#include <QDir>

namespace {
QString findRepositoryRootImpl() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        // 以标志目录存在性判定仓库根，不依赖任何具体样例文件名，
        // 这样题目替换/删除样例数据不会破坏根定位。
        if (dir.exists(QStringLiteral("shared/cases"))
            && dir.exists(QStringLiteral("ground_station_computer/src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::current().absolutePath();
}
} // namespace

namespace RepositoryPaths {

const QString &root() {
    static const QString cached_root = findRepositoryRootImpl();
    return cached_root;
}

QString resolve(const QString &relative_path) {
    return QDir(root()).filePath(relative_path);
}

} // namespace RepositoryPaths
