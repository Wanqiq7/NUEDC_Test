#include "framework/task/competition_task_registry.h"

#include "framework/task/competition_task_adapter.h"

// 题目装配点：框架注册表在此汇总各题目的 descriptor。新增题目只需在
// availableCompetitionTaskAdapters() 里 append 其 descriptor 提供函数，
// 无需改动任何题目已有文件。这是框架 -> 题目方向唯一被允许的 include。
#include "h_problem/h_problem_adapter_registration.h"

#include <QStringList>

QString defaultCompetitionTaskAdapterId() {
    return QStringLiteral("h_problem");
}

QVector<CompetitionTaskAdapterDescriptor> availableCompetitionTaskAdapters() {
    QVector<CompetitionTaskAdapterDescriptor> descriptors;
    descriptors.append(hProblemTaskAdapterDescriptor());
    // 未来题目（如 D 题）在此 append 各自的 descriptor 提供函数。
    return descriptors;
}

QString configuredCompetitionTaskAdapterId() {
    const QString configured_id = qEnvironmentVariable("NUEDC_TASK_ADAPTER").trimmed();
    return configured_id.isEmpty() ? defaultCompetitionTaskAdapterId() : configured_id;
}

std::unique_ptr<CompetitionTaskAdapter> createCompetitionTaskAdapter(
    const QString &adapter_id, QString *error_message) {
    const QString selected_id =
        adapter_id.trimmed().isEmpty() ? defaultCompetitionTaskAdapterId() : adapter_id.trimmed();
    QStringList available_ids;
    for (const CompetitionTaskAdapterDescriptor &descriptor : availableCompetitionTaskAdapters()) {
        available_ids.append(descriptor.adapter_id);
        if (descriptor.adapter_id == selected_id) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return descriptor.create();
        }
    }

    if (error_message != nullptr) {
        *error_message = QStringLiteral("unknown task adapter '%1'; available adapters: %2")
                             .arg(selected_id, available_ids.join(QStringLiteral(", ")));
    }
    return nullptr;
}

std::unique_ptr<CompetitionTaskAdapter> createConfiguredCompetitionTaskAdapter(QString *error_message) {
    return createCompetitionTaskAdapter(configuredCompetitionTaskAdapterId(), error_message);
}

std::unique_ptr<CompetitionTaskAdapter> createDefaultCompetitionTaskAdapter() {
    return createCompetitionTaskAdapter(defaultCompetitionTaskAdapterId());
}
