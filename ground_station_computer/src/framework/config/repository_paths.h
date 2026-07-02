#pragma once

#include <QString>

// 仓库路径解析的框架级单一事实源。题目模块与框架共用同一根探测逻辑，避免各处
// 复制 findRepositoryRoot。探测以「标志目录是否存在」为准，不再绑定某个具体样例
// 文件名（如 sample_case.json），因此题目更换样例数据不会破坏根定位。
namespace RepositoryPaths {

// 返回仓库根绝对路径（从可执行文件所在目录向上探测标志目录，探测失败回退当前目录）。
// 结果在进程内缓存。
const QString &root();

// 将相对仓库根的路径拼接为绝对路径。
QString resolve(const QString &relative_path);

} // namespace RepositoryPaths
