#include "framework/runtime/mission_runtime_state.h"

namespace {
QString taskDisplayText(const QString &task_id) {
    return task_id.isEmpty() ? QStringLiteral("未知") : task_id;
}
}

MissionRuntimeControls MissionRuntimeState::controlsFor(const MissionRuntimeInputs &inputs) {
    if (!inputs.command_sync_enabled || !inputs.airborne_online) {
        return MissionRuntimeControls{};
    }

    if (hasAckState(inputs)) {
        const bool ready = ackMatchesActiveTask(inputs) && inputs.acknowledged_mission_loaded;
        return MissionRuntimeControls{
            ready && !inputs.mission_running,
            ready && inputs.mission_running,
            ready,
            ready,
        };
    }

    const bool ready = inputs.mission_synced_to_airborne;
    return MissionRuntimeControls{
        ready && !inputs.mission_running,
        ready && inputs.mission_running,
        ready,
        ready,
    };
}

QString MissionRuntimeState::airborneStatusText(const MissionRuntimeInputs &inputs) {
    if (!inputs.command_sync_enabled) {
        return QStringLiteral("机载状态: 测试模式");
    }
    if (!inputs.airborne_online) {
        return QStringLiteral("机载状态: 离线");
    }
    if (!hasAckState(inputs)) {
        return QStringLiteral("机载状态: 在线");
    }

    const QString loaded_text = inputs.acknowledged_mission_loaded
        ? QStringLiteral("已加载")
        : QStringLiteral("未加载");
    const QString running_text = inputs.mission_running
        ? QStringLiteral("运行中")
        : QStringLiteral("待执行");
    const QString vision_text = inputs.vision_armed
        ? QStringLiteral("视觉瞄准: 已武装")
        : QStringLiteral("视觉瞄准: 未武装");
    const QString match_text = ackMatchesActiveTask(inputs)
        ? QString()
        : QStringLiteral(" | 任务不匹配");

    return QStringLiteral("机载状态: 在线 | 任务: %1 | %2 | %3 | %4 | Ack序列: %5%6")
        .arg(taskDisplayText(inputs.acknowledged_task_id),
             loaded_text,
             running_text,
             vision_text,
             QString::number(inputs.last_accepted_sequence),
             match_text);
}

bool MissionRuntimeState::hasAckState(const MissionRuntimeInputs &inputs) {
    return !inputs.acknowledged_task_id.isEmpty() || inputs.last_accepted_sequence > 0;
}

bool MissionRuntimeState::ackMatchesActiveTask(const MissionRuntimeInputs &inputs) {
    if (inputs.active_task_id.isEmpty() || inputs.acknowledged_task_id.isEmpty()) {
        return true;
    }
    return inputs.active_task_id == inputs.acknowledged_task_id;
}
