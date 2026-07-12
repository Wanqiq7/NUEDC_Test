#pragma once

#include <QString>
#include <QStringList>

#include "competition_core/task/task_ports.h"

class HRoutePlanner {
public:
    competition::TaskPlanningResult generatePlan(const QString &case_path, const QStringList &no_fly_cells) const;
};
