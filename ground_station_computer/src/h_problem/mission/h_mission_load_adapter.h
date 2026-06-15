#pragma once

#include "h_problem/mission/h_route_planner_bridge.h"

class Envelope;

class MissionLoadAdapter {
public:
    static Envelope buildMissionLoadEnvelope(const MissionPlanData &plan);
};
