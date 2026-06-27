#pragma once

#include <QtGlobal>

#include "h_problem/mission/h_route_planner_bridge.h"

class Envelope;

class MissionLoadAdapter {
public:
    static Envelope buildMissionLoadEnvelope(const MissionPlanData &plan);
    static Envelope buildMissionLoadEnvelope(quint64 sequence, const MissionPlanData &plan);
};
