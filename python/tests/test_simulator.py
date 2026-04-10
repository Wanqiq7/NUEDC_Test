import pathlib
import sys
import unittest


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PYTHON_ROOT = PROJECT_ROOT / "python"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from uav_testbed.simulator import build_case_from_dict, simulate_messages  # noqa: E402


class SimulatorTests(unittest.TestCase):
    def test_simulation_emits_detection_and_summary_for_animals(self) -> None:
        case = build_case_from_dict(
            {
                "case_id": "sample",
                "start_cell": "A1B1",
                "no_fly_cells": ["A4B3", "A5B3", "A6B3"],
                "tick_interval_ms": 10,
                "animals": [
                    {"cell": "A2B1", "name": "elephant", "count": 1},
                    {"cell": "A3B1", "name": "monkey", "count": 2},
                ],
            }
        )

        messages = list(simulate_messages(case))
        detection_types = [msg["message_type"] for msg in messages if msg["message_type"] == "detection"]

        self.assertEqual(detection_types, ["detection", "detection"])
        summary = messages[-1]
        self.assertEqual(summary["message_type"], "summary")
        self.assertEqual(summary["totals"], {"elephant": 1, "monkey": 2})

    def test_build_case_from_dict_parses_landing_profile_and_config_message(self) -> None:
        case = build_case_from_dict(
            {
                "case_id": "landing-demo",
                "start_cell": "A9B1",
                "no_fly_cells": ["A4B3", "A5B3", "A6B3"],
                "tick_interval_ms": 10,
                "landing": {
                    "takeoff_anchor_cm": [450.0, 350.0],
                    "cruise_height_cm": 120.0,
                    "descent_angle_deg": 45.0,
                    "touchdown_radius_cm": 18.0,
                    "preferred_heading_deg": -45.0
                },
                "animals": [],
            }
        )

        self.assertIsNotNone(case.landing)
        messages = list(simulate_messages(case))
        config = messages[0]
        self.assertEqual(config["message_type"], "config")
        self.assertIn("terminal_cell", config)
        self.assertTrue(config["landing_enabled"])

    def test_simulation_prefers_provided_mission_plan(self) -> None:
        case = build_case_from_dict(
            {
                "case_id": "plan-test",
                "start_cell": "A1B1",
                "no_fly_cells": [],
                "tick_interval_ms": 10,
                "animals": [
                    {"cell": "A2B1", "name": "elephant", "count": 1},
                    {"cell": "A3B1", "name": "monkey", "count": 2},
                ],
            }
        )
        plan = {
            "message_type": "config",
            "case_id": "plan-test",
            "start_cell": "A1B1",
            "no_fly_cells": ["A1B2"],
            "route": ["A1B1", "A2B1", "A3B1"],
            "terminal_cell": "A3B1",
            "landing_enabled": False,
            "descent_angle_deg": None,
            "takeoff_anchor_x_cm": None,
            "takeoff_anchor_y_cm": None,
        }

        messages = list(simulate_messages(case, mission_plan=plan))
        config = messages[0]
        self.assertEqual(config["message_type"], "config")
        self.assertEqual(config["no_fly_cells"], plan["no_fly_cells"])
        self.assertEqual(config["route"], plan["route"])
        telemetry_cells = [msg["cell"] for msg in messages if msg["message_type"] == "telemetry"]
        self.assertEqual(telemetry_cells, plan["route"])


if __name__ == "__main__":
    unittest.main()
