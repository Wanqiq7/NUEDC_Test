#pragma once

#include "airborne_runtime.h"

#include <memory>

#include <QString>
#include <QStringList>

namespace airborne {

QStringList availableMissionRuntimeIds();
QString defaultMissionRuntimeId();

std::unique_ptr<MissionRuntime> createMissionRuntime(
    const QString &task_id,
    const QString &case_path,
    const QString &mission_plan_path,
    EventPublisher *publisher,
    QString *error_message = nullptr);

} // namespace airborne
