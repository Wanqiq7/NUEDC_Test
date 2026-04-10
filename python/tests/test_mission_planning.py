import pathlib
import sys
import unittest

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PYTHON_ROOT = PROJECT_ROOT / "python"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))

from uav_testbed.case_loader import load_case
from uav_testbed.mission_planning import build_mission_plan
from uav_testbed.simulator import build_case_from_dict, simulate_messages


class MissionPlanningTests(unittest.TestCase):
    def test_override_no_fly_cells_excludes_route_cells(self) -> None:
        case = build_case_from_dict(
            {
                "case_id": "override-test",
                "start_cell": "A1B1",
                "tick_interval_ms": 10,
                "animals": [],
            }
        )

        override_cells = ["A2B1", "A3B1"]
        override_plan = build_mission_plan(case, override_no_fly_cells=override_cells)
        self.assertEqual(override_plan["no_fly_cells"], override_cells)
        self.assertEqual(override_plan["route"][0], case.start_cell)
        self.assertFalse(set(override_cells) & set(override_plan["route"]))

    def test_plan_matches_simulator_config_payload(self) -> None:
        sample_case = load_case(PROJECT_ROOT / "cases" / "sample_case.json")

        plan = build_mission_plan(sample_case)
        generator = simulate_messages(sample_case)
        config_payload = next(generator)

        self.assertEqual(plan, config_payload)

        expected_keys = {
            "message_type",
            "case_id",
            "start_cell",
            "no_fly_cells",
            "route",
            "terminal_cell",
            "landing_enabled",
            "descent_angle_deg",
            "takeoff_anchor_x_cm",
            "takeoff_anchor_y_cm",
        }
        self.assertTrue(expected_keys.issubset(set(plan.keys())))


if __name__ == "__main__":
    unittest.main()
