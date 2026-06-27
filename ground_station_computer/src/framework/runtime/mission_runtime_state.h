#pragma once

#include <QString>
#include <QtGlobal>

struct MissionRuntimeInputs {
    bool command_sync_enabled = false;
    bool airborne_online = false;
    bool mission_synced_to_airborne = false;
    bool mission_running = false;
    QString active_task_id;
    QString acknowledged_task_id;
    bool acknowledged_mission_loaded = false;
    quint64 last_accepted_sequence = 0;
};

struct MissionRuntimeControls {
    bool can_execute = false;
    bool can_stop = false;
};

class MissionRuntimeState {
public:
    static MissionRuntimeControls controlsFor(const MissionRuntimeInputs &inputs);
    static QString airborneStatusText(const MissionRuntimeInputs &inputs);

private:
    static bool hasAckState(const MissionRuntimeInputs &inputs);
    static bool ackMatchesActiveTask(const MissionRuntimeInputs &inputs);
};

