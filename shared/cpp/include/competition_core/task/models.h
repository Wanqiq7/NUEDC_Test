#pragma once

#include <QString>
#include <QVector>

namespace competition {

struct TaskDefinition {
    QString task_id;
    QString task_type;
    QString metadata_json;
};

struct TaskWaypoint {
    QString id;
    quint32 sequence_index = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    QString action;
    QString payload_json;
};

struct TaskPlan {
    QString task_id;
    QString task_type;
    QString start_waypoint_id;
    QString terminal_waypoint_id;
    QVector<TaskWaypoint> waypoints;
    QString metadata_json;
};

struct TaskEvent {
    QString task_id;
    QString event_type;
    quint32 sequence_index = 0;
    QString waypoint_id;
    QString payload_json;
};

struct TaskSummary {
    QString task_id;
    QString task_type;
    bool success = true;
    quint32 visited_waypoints = 0;
    QString payload_json;
};

struct AckResult {
    bool success = false;
    QString message;
};

struct CommandState {
    bool start_requested = false;
    bool stop_requested = false;
    bool mission_loaded = false;
    TaskPlan active_task_plan;
};

} // namespace competition
