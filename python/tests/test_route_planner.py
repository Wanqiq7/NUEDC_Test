import pathlib
import sys
import unittest


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PYTHON_ROOT = PROJECT_ROOT / "python"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from uav_testbed.route_planner import _exact_completion_to_end, plan_route  # noqa: E402


class RoutePlannerTests(unittest.TestCase):
    @staticmethod
    def _to_point(cell: str) -> tuple[int, int]:
        a_index = cell.index("A")
        b_index = cell.index("B")
        return int(cell[a_index + 1:b_index]) - 1, int(cell[b_index + 1:]) - 1

    def test_plan_route_covers_every_non_forbidden_cell_with_adjacent_steps(self) -> None:
        blocked = {"A4B3", "A5B3", "A6B3"}
        route = plan_route(width=9, height=7, start_cell="A1B1", no_fly_cells=blocked)

        self.assertEqual(route[0], "A1B1")
        for blocked_cell in blocked:
            self.assertNotIn(blocked_cell, route)

        visited = set(route)
        self.assertEqual(len(visited), 60)

        for previous, current in zip(route, route[1:]):
            previous_point = self._to_point(previous)
            current_point = self._to_point(current)
            manhattan_distance = abs(previous_point[0] - current_point[0]) + abs(previous_point[1] - current_point[1])
            self.assertEqual(
                manhattan_distance,
                1,
                msg=f"route contains illegal jump: {previous} -> {current}",
            )

    def test_plan_route_keeps_sample_case_route_length_at_or_below_61(self) -> None:
        route = plan_route(
            width=9,
            height=7,
            start_cell="A1B1",
            no_fly_cells={"A4B3", "A5B3", "A6B3"},
        )

        self.assertLessEqual(
            len(route),
            61,
            msg=f"optimized sample route regressed to {len(route)} cells",
        )

    def test_plan_route_supports_closed_tour_back_to_start(self) -> None:
        route = plan_route(
            width=9,
            height=7,
            start_cell="A9B7",
            no_fly_cells={"A4B3", "A5B3", "A6B3"},
            end_cell="A9B7",
            require_cycle=True,
        )

        self.assertEqual(route[0], "A9B7")
        self.assertEqual(route[-1], "A9B7")
        self.assertEqual(len(set(route)), 60)

        for previous, current in zip(route, route[1:]):
            previous_point = self._to_point(previous)
            current_point = self._to_point(current)
            manhattan_distance = abs(previous_point[0] - current_point[0]) + abs(previous_point[1] - current_point[1])
            self.assertEqual(
                manhattan_distance,
                1,
                msg=f"closed route contains illegal jump: {previous} -> {current}",
            )

    def test_plan_route_prefers_landing_compatible_terminal_region(self) -> None:
        from uav_testbed.mission_geometry import LandingProfile, PointCm, terminal_cells_for_landing

        landing_profile = LandingProfile(
            takeoff_anchor_cm=PointCm(x_cm=450.0, y_cm=350.0),
            cruise_height_cm=120.0,
            descent_angle_deg=45.0,
            touchdown_radius_cm=18.0,
        )
        route = plan_route(
            width=9,
            height=7,
            start_cell="A9B1",
            no_fly_cells={"A4B3", "A5B3", "A6B3"},
            mission_mode="time_optimal_open",
            landing_profile=landing_profile,
        )

        self.assertIn(route[0], {"A9B1", "A8B1", "A9B2"})
        self.assertIn(
            route[-1],
            terminal_cells_for_landing(
                width=9,
                height=7,
                no_fly_cells={"A4B3", "A5B3", "A6B3"},
                landing_profile=landing_profile,
            ),
        )
        self.assertEqual(len(set(route)), 60)

    def test_time_optimal_open_route_beats_closed_cycle_cost(self) -> None:
        from uav_testbed.mission_geometry import LandingProfile, PointCm
        from uav_testbed.route_cost import estimate_route_cost

        closed_route = plan_route(
            width=9,
            height=7,
            start_cell="A9B7",
            no_fly_cells={"A4B3", "A5B3", "A6B3"},
            end_cell="A9B7",
            require_cycle=True,
        )
        open_route = plan_route(
            width=9,
            height=7,
            start_cell="A9B1",
            no_fly_cells={"A4B3", "A5B3", "A6B3"},
            mission_mode="time_optimal_open",
            landing_profile=LandingProfile(
                takeoff_anchor_cm=PointCm(x_cm=450.0, y_cm=350.0),
                cruise_height_cm=120.0,
                descent_angle_deg=45.0,
                touchdown_radius_cm=18.0,
            ),
        )

        landing_profile = LandingProfile(
            takeoff_anchor_cm=PointCm(x_cm=450.0, y_cm=350.0),
            cruise_height_cm=120.0,
            descent_angle_deg=45.0,
            touchdown_radius_cm=18.0,
        )
        self.assertLess(
            estimate_route_cost(open_route, landing_profile=landing_profile),
            estimate_route_cost(closed_route, landing_profile=landing_profile),
        )

    def test_plan_route_finds_exact_path_for_small_grid_when_hamilton_path_exists(self) -> None:
        route = plan_route(
            width=4,
            height=4,
            start_cell="A1B1",
            no_fly_cells={"A4B1"},
        )

        self.assertEqual(len(set(route)), 15)
        self.assertEqual(
            len(route),
            15,
            msg=f"planner should find the exact Hamilton path on the small grid, got {len(route)}",
        )

    def test_exact_completion_to_end_finds_shortest_tail_on_small_grid(self) -> None:
        completion = _exact_completion_to_end(
            width=3,
            height=3,
            current_cell="A1B1",
            required_cells=("A2B1", "A3B1"),
            end_cell="A3B3",
            no_fly_cells=set(),
        )

        self.assertEqual(
            completion,
            ["A1B1", "A2B1", "A3B1", "A3B2", "A3B3"],
        )

    def test_estimate_route_cost_penalizes_detours_and_turns(self) -> None:
        from uav_testbed.route_cost import estimate_route_cost

        direct_route = ["A1B1", "A2B1", "A3B1"]
        detour_route = ["A1B1", "A1B2", "A2B2", "A2B1", "A3B1"]
        self.assertLess(estimate_route_cost(direct_route), estimate_route_cost(detour_route))


if __name__ == "__main__":
    unittest.main()
