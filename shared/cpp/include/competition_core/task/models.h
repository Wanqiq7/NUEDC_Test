#pragma once

#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
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
    QString task_id;
    bool mission_loaded = false;
    bool mission_running = false;
    quint64 last_accepted_sequence = 0;
    bool vision_armed = false;
};

struct CommandState {
    CommandState() = default;

    CommandState(const CommandState &other)
        : start_requested_(other.isStartRequested()),
          stop_requested_(other.isStopRequested()),
          mission_loaded_(other.isMissionLoaded()),
          vision_targeting_armed_(other.isVisionTargetingArmed()),
          last_accepted_sequence_(other.lastAcceptedSequence()),
          active_task_plan_(other.activeTaskPlan()) {}

    CommandState &operator=(const CommandState &other) {
        if (this == &other) {
            return *this;
        }
        start_requested_.store(other.isStartRequested(), std::memory_order_relaxed);
        stop_requested_.store(other.isStopRequested(), std::memory_order_relaxed);
        mission_loaded_.store(other.isMissionLoaded(), std::memory_order_relaxed);
        vision_targeting_armed_.store(other.isVisionTargetingArmed(), std::memory_order_relaxed);
        last_accepted_sequence_.store(other.lastAcceptedSequence(), std::memory_order_relaxed);
        setActiveTaskPlan(other.activeTaskPlan());
        return *this;
    }

    void requestStart() {
        stop_requested_.store(false, std::memory_order_release);
        start_requested_.store(true, std::memory_order_release);
    }

    void requestStop() {
        stop_requested_.store(true, std::memory_order_release);
        resetVisionTargeting();
    }

    bool isStartRequested() const {
        return start_requested_.load(std::memory_order_acquire);
    }

    bool isStopRequested() const {
        return stop_requested_.load(std::memory_order_acquire);
    }

    void armVisionTargeting() {
        vision_targeting_armed_.store(true, std::memory_order_release);
    }

    void resetVisionTargeting() {
        vision_targeting_armed_.store(false, std::memory_order_release);
    }

    bool isVisionTargetingArmed() const {
        return vision_targeting_armed_.load(std::memory_order_acquire);
    }

    void setMissionLoaded(bool loaded) {
        mission_loaded_.store(loaded, std::memory_order_release);
    }

    bool isMissionLoaded() const {
        return mission_loaded_.load(std::memory_order_acquire);
    }

    void replaceMission(const TaskPlan &plan) {
        QMutexLocker<QMutex> locker(&active_task_plan_mutex_);
        mission_loaded_.store(false, std::memory_order_release);
        active_task_plan_ = plan;
        start_requested_.store(false, std::memory_order_release);
        stop_requested_.store(false, std::memory_order_release);
        vision_targeting_armed_.store(false, std::memory_order_release);
        mission_loaded_.store(true, std::memory_order_release);
    }

    void completeMission() {
        mission_loaded_.store(false, std::memory_order_release);
        start_requested_.store(false, std::memory_order_release);
        stop_requested_.store(false, std::memory_order_release);
        vision_targeting_armed_.store(false, std::memory_order_release);
    }

    void setActiveTaskPlan(const TaskPlan &plan) {
        QMutexLocker<QMutex> locker(&active_task_plan_mutex_);
        active_task_plan_ = plan;
    }

    TaskPlan activeTaskPlan() const {
        QMutexLocker<QMutex> locker(&active_task_plan_mutex_);
        return active_task_plan_;
    }

    quint64 lastAcceptedSequence() const {
        return last_accepted_sequence_.load(std::memory_order_acquire);
    }

    bool isStaleSequence(quint64 sequence) const {
        return sequence != 0 && sequence <= lastAcceptedSequence();
    }


    void acceptSequence(quint64 sequence) {
        if (sequence != 0) {
            last_accepted_sequence_.store(sequence, std::memory_order_release);
        }
    }

    QMutex *commandMutex() {
        return &command_mutex_;
    }

private:
    std::atomic_bool start_requested_{false};
    std::atomic_bool stop_requested_{false};
    std::atomic_bool mission_loaded_{false};
    std::atomic_bool vision_targeting_armed_{false};
    std::atomic<quint64> last_accepted_sequence_{0};
    mutable QMutex command_mutex_;
    mutable QMutex active_task_plan_mutex_;
    TaskPlan active_task_plan_;
};

} // namespace competition

Q_DECLARE_METATYPE(competition::TaskPlan)
Q_DECLARE_METATYPE(competition::TaskEvent)
Q_DECLARE_METATYPE(competition::TaskSummary)
