#include "framework/runtime/airborne_sync_state.h"

void AirborneSyncState::clearAck() {
    acknowledged_task_id_.clear();
    acknowledged_mission_loaded_ = false;
    last_accepted_sequence_ = 0;
}

void AirborneSyncState::reset() {
    synced_to_airborne_ = false;
    running_ = false;
    clearAck();
}

bool AirborneSyncState::applyCommandAck(const CommandSendResult &result) {
    if (!result.ok || (result.task_id.isEmpty() && result.last_accepted_sequence == 0)) {
        return false;
    }

    if (!result.task_id.isEmpty()) {
        acknowledged_task_id_ = result.task_id;
    }
    acknowledged_mission_loaded_ = result.mission_loaded;
    synced_to_airborne_ = result.mission_loaded;
    running_ = result.mission_running;
    last_accepted_sequence_ = result.last_accepted_sequence;
    return true;
}

void AirborneSyncState::applyAirborneSync(bool online, bool synced) {
    if (!online) {
        clearAck();
    }
    synced_to_airborne_ = synced;
    if (!synced) {
        running_ = false;
        acknowledged_mission_loaded_ = false;
    }
}

void AirborneSyncState::markControlStarted() {
    running_ = true;
    acknowledged_mission_loaded_ = true;
}

void AirborneSyncState::markControlStopped() {
    running_ = false;
}

void AirborneSyncState::fillRuntimeInputs(MissionRuntimeInputs &inputs) const {
    inputs.mission_synced_to_airborne = synced_to_airborne_;
    inputs.mission_running = running_;
    inputs.acknowledged_task_id = acknowledged_task_id_;
    inputs.acknowledged_mission_loaded = acknowledged_mission_loaded_;
    inputs.last_accepted_sequence = last_accepted_sequence_;
}
