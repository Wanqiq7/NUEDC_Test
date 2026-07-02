#pragma once

#include "framework/communication/envelope_codec.h"   // CommandSendResult
#include "framework/runtime/mission_runtime_state.h"   // MissionRuntimeInputs

#include <QString>
#include <QtGlobal>

// 机载同步 / 运行状态的单一事实源。此前这 5 个字段以裸标量散落在题目 Adapter，
// 在 6 处手工同步，容易出现「漏清某个字段」的状态错配。这里把状态与其迁移规则
// 收敛到一个纯值类型：不持有任何 UI、不发信号，因此可脱 Qt Widgets 单测；
// UI 刷新仍由调用方（Adapter）在迁移后触发。语义与原 Adapter 实现 1:1 对应。
class AirborneSyncState {
public:
    // 仅清除 ack 相关字段（= 原 clearCommandAckState），不触碰 synced / running。
    void clearAck();

    // 完整复位：synced=false、running=false，并清除 ack。对应原来分散的
    // 「mission_synced_to_airborne_=false; mission_running_=false; clearCommandAckState();」组合。
    void reset();

    // 应用机载端 Ack。无效 Ack（!ok 或 task_id 空且 seq==0）不改变既有状态，
    // 返回 false 表示未应用（调用方据此决定是否刷新 UI）；成功应用返回 true。
    bool applyCommandAck(const CommandSendResult &result);

    // 机载在线 / 同步状态变化（= 原 markAirborneSyncState 的状态部分）。
    void applyAirborneSync(bool online, bool synced);

    // 控制指令已开始 / 已停止（= 原 markControlCommandStarted / Stopped 的状态部分）。
    void markControlStarted();
    void markControlStopped();

    // 单字段设置，对应事件处理流里的内联赋值（applyGridConfig / applyTelemetry / applySummary
    // 以及本地生成成功后的置位），保持逐字段迁移的忠实度。
    void setSyncedToAirborne(bool synced) { synced_to_airborne_ = synced; }
    void setRunning(bool running) { running_ = running; }

    // 只读查询。
    bool syncedToAirborne() const { return synced_to_airborne_; }
    bool running() const { return running_; }
    const QString &acknowledgedTaskId() const { return acknowledged_task_id_; }
    bool acknowledgedMissionLoaded() const { return acknowledged_mission_loaded_; }
    quint64 lastAcceptedSequence() const { return last_accepted_sequence_; }
    bool hasAck() const { return !acknowledged_task_id_.isEmpty() || last_accepted_sequence_ > 0; }

    // 把自身拥有的字段投射进通用运行态输入；调用方另行补齐 command_sync_enabled /
    // airborne_online / active_task_id 等非本类字段。
    void fillRuntimeInputs(MissionRuntimeInputs &inputs) const;

private:
    bool synced_to_airborne_ = false;
    bool running_ = false;
    QString acknowledged_task_id_;
    bool acknowledged_mission_loaded_ = false;
    quint64 last_accepted_sequence_ = 0;
};
