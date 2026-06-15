#pragma once

#include <QString>

#include "h_problem/mission/h_route_planner_bridge.h"

class MissionPlanStore {
public:
    explicit MissionPlanStore(QString output_path);

    bool save(const MissionPlanData &plan, QString *error_message = nullptr) const;

private:
    QString output_path_;
};
