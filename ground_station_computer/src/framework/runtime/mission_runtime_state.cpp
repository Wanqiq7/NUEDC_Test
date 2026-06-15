#include "framework/runtime/mission_runtime_state.h"

MissionRuntimeControls MissionRuntimeState::controlsFor(const MissionRuntimeInputs &inputs) {
    return MissionRuntimeControls{
        inputs.command_sync_enabled && inputs.airborne_online && inputs.mission_synced_to_airborne &&
            !inputs.mission_running,
        inputs.command_sync_enabled && inputs.airborne_online && inputs.mission_running,
    };
}
