#include "h_problem/mission/h_mission_load_adapter.h"

#include "h_problem_core/protocol/envelope_builder.h"

Envelope MissionLoadAdapter::buildMissionLoadEnvelope(const MissionPlanData &plan) {
    hcore::MissionPlan mission_plan;
    mission_plan.case_id = plan.case_id;
    mission_plan.start_cell = plan.start_cell;
    mission_plan.no_fly_cells = plan.no_fly_cells;
    mission_plan.route = plan.route;
    mission_plan.terminal_cell = plan.terminal_cell;
    mission_plan.landing_enabled = plan.landing_enabled;
    mission_plan.descent_angle_deg = plan.descent_angle_deg;
    mission_plan.takeoff_anchor_x_cm = plan.takeoff_anchor_x_cm;
    mission_plan.takeoff_anchor_y_cm = plan.takeoff_anchor_y_cm;
    return hcore::buildMissionLoadEnvelope(mission_plan);
}
