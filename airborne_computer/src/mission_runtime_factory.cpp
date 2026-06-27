#include "mission_runtime_factory.h"

#include "h_problem_mission_runtime.h"
#include "h_problem_core/mission/case_loader.h"

#include <QDebug>

namespace airborne {

QStringList availableMissionRuntimeIds() {
    return {"h_problem"};
}

QString defaultMissionRuntimeId() {
    return "h_problem";
}

std::unique_ptr<MissionRuntime> createMissionRuntime(
    const QString &task_id,
    const QString &case_path,
    const QString &mission_plan_path,
    EventPublisher *publisher,
    QString *error_message) {
    const QString selected_id = task_id.trimmed().isEmpty() ? defaultMissionRuntimeId() : task_id.trimmed();
    if (selected_id == "h_problem") {
        QString error;
        const auto case_config = hcore::loadCase(case_path, &error);
        if (!case_config.has_value()) {
            if (error_message != nullptr) {
                *error_message = QString("无法加载 H 题案例: %1").arg(error);
            }
            return nullptr;
        }

        const auto stored_plan = loadOptionalMissionPlan(mission_plan_path, &error);
        if (!error.isEmpty()) {
            qWarning("无法加载已保存任务计划 %s: %s", qPrintable(mission_plan_path), qPrintable(error));
            error.clear();
        }
        const hcore::MissionPlan selected_plan = selectMissionPlan(case_config.value(), stored_plan, &error);
        if (!error.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QString("无法生成 H 题任务计划: %1").arg(error);
            }
            return nullptr;
        }

        if (error_message != nullptr) {
            error_message->clear();
        }
        return std::make_unique<HProblemMissionRuntime>(case_config.value(), selected_plan, publisher);
    }

    if (error_message != nullptr) {
        *error_message = QString("unknown mission runtime '%1'; available runtimes: %2")
                             .arg(selected_id, availableMissionRuntimeIds().join(", "));
    }
    return nullptr;
}

} // namespace airborne
