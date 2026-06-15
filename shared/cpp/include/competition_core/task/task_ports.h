#pragma once

#include "competition_core/task/models.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace competition {

struct TaskPlanningRequest {
    QString adapter_id;
    QString task_id;
    QString task_type;
    QString case_path;
    QString metadata_json;
    QVariantMap constraints;
};

struct TaskPlanningResult {
    bool ok = false;
    TaskPlan plan;
    double estimated_cost = 0.0;
    double coverage_rate = 0.0;
    QStringList warnings;
    QString failure_reason;

    static TaskPlanningResult success(const TaskPlan &task_plan) {
        TaskPlanningResult result;
        result.ok = true;
        result.plan = task_plan;
        return result;
    }

    static TaskPlanningResult failure(const QString &reason) {
        TaskPlanningResult result;
        result.ok = false;
        result.failure_reason = reason;
        return result;
    }
};

class TaskPlanner {
public:
    virtual ~TaskPlanner() = default;

    virtual TaskPlanningResult planTask(const TaskPlanningRequest &request) const = 0;
};

class TaskCodec {
public:
    virtual ~TaskCodec() = default;

    virtual QString adapterId() const = 0;
    virtual QString encodeMetadata(const QVariantMap &metadata) const = 0;
};

struct TaskAdapterDescriptor {
    QString adapter_id;
    QString display_name;
};

} // namespace competition
