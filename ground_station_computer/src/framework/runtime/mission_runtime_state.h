#pragma once

struct MissionRuntimeInputs {
    bool command_sync_enabled = false;
    bool airborne_online = false;
    bool mission_synced_to_airborne = false;
    bool mission_running = false;
};

struct MissionRuntimeControls {
    bool can_execute = false;
    bool can_stop = false;
};

class MissionRuntimeState {
public:
    static MissionRuntimeControls controlsFor(const MissionRuntimeInputs &inputs);
};
