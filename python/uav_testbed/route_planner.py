from __future__ import annotations

from collections.abc import Iterable
from collections import deque
import heapq
import time

from .mission_geometry import LandingProfile, terminal_cells_for_landing
from .route_cost import estimate_route_cost


def _encode_cell(x_index: int, y_index: int) -> str:
    return f"A{x_index + 1}B{y_index + 1}"


def _decode_cell(cell_code: str) -> tuple[int, int]:
    a_index = cell_code.index("A")
    b_index = cell_code.index("B")
    return int(cell_code[a_index + 1:b_index]) - 1, int(cell_code[b_index + 1:]) - 1


def _ordered_neighbors(
    cell: str,
    width: int,
    height: int,
    visited: set[str],
    no_fly_cells: set[str],
) -> Iterable[str]:
    x_index, y_index = _decode_cell(cell)
    if y_index % 2 == 0:
        candidates = ((x_index + 1, y_index), (x_index, y_index + 1), (x_index - 1, y_index), (x_index, y_index - 1))
    else:
        candidates = ((x_index - 1, y_index), (x_index, y_index + 1), (x_index + 1, y_index), (x_index, y_index - 1))

    ranked_candidates: list[tuple[int, int, int, str]] = []
    for next_x, next_y in candidates:
        if 0 <= next_x < width and 0 <= next_y < height:
            next_cell = _encode_cell(next_x, next_y)
            if next_cell not in no_fly_cells and next_cell not in visited:
                branch_size = _reachable_component_size(
                    start_cell=next_cell,
                    width=width,
                    height=height,
                    blocked_cells=no_fly_cells | visited | {cell},
                )
                ranked_candidates.append((branch_size, next_y, next_x, next_cell))

    ranked_candidates.sort()
    for _, _, _, next_cell in ranked_candidates:
        yield next_cell


def _reachable_component_size(
    start_cell: str,
    width: int,
    height: int,
    blocked_cells: set[str],
) -> int:
    if start_cell in blocked_cells:
        return 0

    stack = [start_cell]
    seen = {start_cell}
    while stack:
        cell = stack.pop()
        x_index, y_index = _decode_cell(cell)
        for next_x, next_y in (
            (x_index + 1, y_index),
            (x_index - 1, y_index),
            (x_index, y_index + 1),
            (x_index, y_index - 1),
        ):
            if 0 <= next_x < width and 0 <= next_y < height:
                next_cell = _encode_cell(next_x, next_y)
                if next_cell in blocked_cells or next_cell in seen:
                    continue
                seen.add(next_cell)
                stack.append(next_cell)

    return len(seen)


def _is_remaining_graph_connected(
    nodes: list[str],
    adjacency: dict[str, tuple[str, ...]],
    visited: set[str],
) -> bool:
    remaining_nodes = [node for node in nodes if node not in visited]
    if not remaining_nodes:
        return True

    remaining_set = set(remaining_nodes)
    stack = [remaining_nodes[0]]
    seen = {remaining_nodes[0]}
    while stack:
        node = stack.pop()
        for neighbor in adjacency[node]:
            if neighbor in remaining_set and neighbor not in seen:
                seen.add(neighbor)
                stack.append(neighbor)
    return len(seen) == len(remaining_nodes)


def _attempt_exact_hamilton_path(
    width: int,
    height: int,
    start_cell: str,
    no_fly_cells: set[str],
    time_budget_seconds: float,
) -> list[str] | None:
    nodes = [
        _encode_cell(x_index, y_index)
        for y_index in range(height)
        for x_index in range(width)
        if _encode_cell(x_index, y_index) not in no_fly_cells
    ]
    adjacency: dict[str, tuple[str, ...]] = {
        node: tuple(_ordered_neighbors(node, width, height, set(), no_fly_cells))
        for node in nodes
    }

    deadline = time.monotonic() + time_budget_seconds
    visited = {start_cell}
    path = [start_cell]

    def dfs(cell: str) -> list[str] | None:
        if time.monotonic() > deadline:
            return None
        if len(path) == len(nodes):
            return path.copy()
        if not _is_remaining_graph_connected(nodes, adjacency, visited):
            return None

        candidates = [neighbor for neighbor in adjacency[cell] if neighbor not in visited]
        candidates.sort(
            key=lambda neighbor: (
                sum(1 for next_neighbor in adjacency[neighbor] if next_neighbor not in visited),
                _decode_cell(neighbor)[1],
                _decode_cell(neighbor)[0],
            )
        )
        for neighbor in candidates:
            visited.add(neighbor)
            path.append(neighbor)
            exact_path = dfs(neighbor)
            if exact_path is not None:
                return exact_path
            path.pop()
            visited.remove(neighbor)
        return None

    return dfs(start_cell)


def _attempt_exact_hamilton_cycle(
    width: int,
    height: int,
    start_cell: str,
    no_fly_cells: set[str],
    time_budget_seconds: float,
) -> list[str] | None:
    nodes = [
        _encode_cell(x_index, y_index)
        for y_index in range(height)
        for x_index in range(width)
        if _encode_cell(x_index, y_index) not in no_fly_cells
    ]
    adjacency: dict[str, tuple[str, ...]] = {
        node: tuple(_ordered_neighbors(node, width, height, set(), no_fly_cells))
        for node in nodes
    }

    deadline = time.monotonic() + time_budget_seconds
    visited = {start_cell}
    path = [start_cell]

    def dfs(cell: str) -> list[str] | None:
        if time.monotonic() > deadline:
            return None
        if len(path) == len(nodes):
            if start_cell in adjacency[cell]:
                return path.copy() + [start_cell]
            return None
        if not _is_remaining_graph_connected(nodes, adjacency, visited):
            return None

        candidates = [neighbor for neighbor in adjacency[cell] if neighbor not in visited]
        candidates.sort(
            key=lambda neighbor: (
                sum(1 for next_neighbor in adjacency[neighbor] if next_neighbor not in visited),
                _decode_cell(neighbor)[1],
                _decode_cell(neighbor)[0],
            )
        )
        for neighbor in candidates:
            visited.add(neighbor)
            path.append(neighbor)
            exact_cycle = dfs(neighbor)
            if exact_cycle is not None:
                return exact_cycle
            path.pop()
            visited.remove(neighbor)
        return None

    return dfs(start_cell)


def _shortest_path(
    width: int,
    height: int,
    start_cell: str,
    end_cell: str,
    no_fly_cells: set[str],
) -> list[str]:
    queue = deque([start_cell])
    parent = {start_cell: None}

    while queue:
        cell = queue.popleft()
        if cell == end_cell:
            break
        for neighbor in _ordered_neighbors(cell, width, height, set(), no_fly_cells):
            if neighbor in parent:
                continue
            parent[neighbor] = cell
            queue.append(neighbor)

    if end_cell not in parent:
        raise ValueError(f"no path from {start_cell} to {end_cell}")

    path = []
    current = end_cell
    while current is not None:
        path.append(current)
        current = parent[current]
    path.reverse()
    return path


def _grid_neighbors(
    cell: str,
    width: int,
    height: int,
    no_fly_cells: set[str],
) -> tuple[str, ...]:
    x_index, y_index = _decode_cell(cell)
    neighbors: list[str] = []
    for next_x, next_y in (
        (x_index + 1, y_index),
        (x_index - 1, y_index),
        (x_index, y_index + 1),
        (x_index, y_index - 1),
    ):
        if 0 <= next_x < width and 0 <= next_y < height:
            next_cell = _encode_cell(next_x, next_y)
            if next_cell not in no_fly_cells:
                neighbors.append(next_cell)
    return tuple(neighbors)


def _exact_completion_to_end(
    width: int,
    height: int,
    current_cell: str,
    required_cells: tuple[str, ...],
    end_cell: str,
    no_fly_cells: set[str],
) -> list[str] | None:
    terminal_index = {cell: index for index, cell in enumerate(required_cells)}
    full_mask = (1 << len(required_cells)) - 1
    start_mask = 0
    if current_cell in terminal_index:
        start_mask |= 1 << terminal_index[current_cell]

    distance_to_end = {end_cell: 0}
    queue = deque([end_cell])
    while queue:
        cell = queue.popleft()
        for neighbor in _grid_neighbors(cell, width, height, no_fly_cells):
            if neighbor in distance_to_end:
                continue
            distance_to_end[neighbor] = distance_to_end[cell] + 1
            queue.append(neighbor)

    start_state = (current_cell, start_mask)
    best_cost: dict[tuple[str, int], int] = {start_state: 0}
    parent: dict[tuple[str, int], tuple[str, int] | None] = {start_state: None}
    priority_queue: list[tuple[int, int, str, int]] = [
        (distance_to_end[current_cell], 0, current_cell, start_mask)
    ]

    while priority_queue:
        _, cost, cell, visited_mask = heapq.heappop(priority_queue)
        if cost != best_cost.get((cell, visited_mask)):
            continue
        if cell == end_cell and visited_mask == full_mask:
            completion: list[str] = []
            state: tuple[str, int] | None = (cell, visited_mask)
            while state is not None:
                completion.append(state[0])
                state = parent[state]
            completion.reverse()
            return completion

        for neighbor in _grid_neighbors(cell, width, height, no_fly_cells):
            next_mask = visited_mask
            terminal_position = terminal_index.get(neighbor)
            if terminal_position is not None:
                next_mask |= 1 << terminal_position

            next_state = (neighbor, next_mask)
            next_cost = cost + 1
            if next_cost >= best_cost.get(next_state, 1_000_000_000):
                continue

            best_cost[next_state] = next_cost
            parent[next_state] = (cell, visited_mask)
            heuristic = distance_to_end[neighbor]
            heapq.heappush(priority_queue, (next_cost + heuristic, next_cost, neighbor, next_mask))

    return None


def _optimize_route_suffix_to_end(
    route: list[str],
    width: int,
    height: int,
    end_cell: str,
    no_fly_cells: set[str],
    max_remaining_cells: int = 16,
    max_cut_candidates: int = 4,
) -> list[str] | None:
    all_cells = {
        _encode_cell(x_index, y_index)
        for y_index in range(height)
        for x_index in range(width)
        if _encode_cell(x_index, y_index) not in no_fly_cells
    }

    best_route = route if route[-1] == end_cell else None
    seen_cells: set[str] = set()
    tested_candidates = 0

    for cut_index, cell in enumerate(route):
        seen_cells.add(cell)
        remaining_cells = tuple(sorted(all_cells - seen_cells))
        if len(remaining_cells) > max_remaining_cells:
            continue

        suffix = _exact_completion_to_end(
            width=width,
            height=height,
            current_cell=cell,
            required_cells=remaining_cells,
            end_cell=end_cell,
            no_fly_cells=no_fly_cells,
        )
        tested_candidates += 1
        if suffix is not None:
            candidate_route = route[:cut_index] + suffix
            if best_route is None or len(candidate_route) < len(best_route):
                best_route = candidate_route

        if tested_candidates >= max_cut_candidates:
            break

    return best_route


def _plan_legacy_route(
    width: int,
    height: int,
    start_cell: str,
    no_fly_cells: set[str],
    end_cell: str | None = None,
    require_cycle: bool = False,
) -> list[str]:
    if start_cell in no_fly_cells:
        raise ValueError("start cell cannot be inside the no-fly zone")
    if end_cell is not None and end_cell in no_fly_cells:
        raise ValueError("end cell cannot be inside the no-fly zone")

    total_cells = (width * height) - len(no_fly_cells)
    if require_cycle:
        cycle_target = end_cell or start_cell
        if cycle_target != start_cell:
            raise ValueError("closed tour currently requires end_cell to match start_cell")
        if total_cells <= 25:
            exact_cycle = _attempt_exact_hamilton_cycle(
                width=width,
                height=height,
                start_cell=start_cell,
                no_fly_cells=no_fly_cells,
                time_budget_seconds=1.0,
            )
            if exact_cycle is not None:
                return exact_cycle

    if total_cells <= 25:
        exact_path = _attempt_exact_hamilton_path(
            width=width,
            height=height,
            start_cell=start_cell,
            no_fly_cells=no_fly_cells,
            time_budget_seconds=1.0,
        )
        if exact_path is not None and (end_cell is None or exact_path[-1] == end_cell):
            return exact_path

    visited = {start_cell}
    spanning_tree: dict[str, list[str]] = {}
    parent: dict[str, str | None] = {start_cell: None}
    depth: dict[str, int] = {start_cell: 0}

    def build_spanning_tree(cell: str) -> None:
        spanning_tree[cell] = []
        ordered_neighbors = list(_ordered_neighbors(cell, width, height, visited, no_fly_cells))
        for neighbor in ordered_neighbors:
            if neighbor in visited:
                continue
            visited.add(neighbor)
            parent[neighbor] = cell
            depth[neighbor] = depth[cell] + 1
            spanning_tree[cell].append(neighbor)
            build_spanning_tree(neighbor)

    build_spanning_tree(start_cell)

    if len(visited) != total_cells:
        raise ValueError("planner failed to cover every reachable non-forbidden cell")

    target_cell = start_cell if require_cycle else end_cell
    leaf_cells = [cell for cell in visited if not spanning_tree.get(cell)]
    if not leaf_cells:
        leaf_cells = [start_cell]

    def shortest_distance(from_cell: str, to_cell: str | None) -> int:
        if to_cell is None:
            return 0
        return len(
            _shortest_path(
                width=width,
                height=height,
                start_cell=from_cell,
                end_cell=to_cell,
                no_fly_cells=no_fly_cells,
            )
        ) - 1

    terminal_leaf = max(
        leaf_cells,
        key=lambda cell: (
            depth[cell] - shortest_distance(cell, target_cell),
            depth[cell],
            _decode_cell(cell)[1],
            _decode_cell(cell)[0],
        ),
    )

    terminal_child_by_parent: dict[str, str] = {}
    current = terminal_leaf
    while parent[current] is not None:
        terminal_child_by_parent[parent[current]] = current
        current = parent[current]

    route = [start_cell]

    def emit_route(cell: str) -> None:
        children = spanning_tree.get(cell, [])
        terminal_child = terminal_child_by_parent.get(cell)

        for child in children:
            if child == terminal_child:
                continue
            route.append(child)
            emit_route(child)
            route.append(cell)

        if terminal_child is not None:
            route.append(terminal_child)
            emit_route(terminal_child)

    emit_route(start_cell)

    if require_cycle:
        optimized_cycle = _optimize_route_suffix_to_end(
            route=route,
            width=width,
            height=height,
            end_cell=start_cell,
            no_fly_cells=no_fly_cells,
        )
        if optimized_cycle is not None:
            return optimized_cycle

        return_path = _shortest_path(
            width=width,
            height=height,
            start_cell=route[-1],
            end_cell=start_cell,
            no_fly_cells=no_fly_cells,
        )
        route.extend(return_path[1:])
        return route

    if end_cell is not None and route[-1] != end_cell:
        optimized_route = _optimize_route_suffix_to_end(
            route=route,
            width=width,
            height=height,
            end_cell=end_cell,
            no_fly_cells=no_fly_cells,
        )
        if optimized_route is not None:
            return optimized_route

        tail_path = _shortest_path(
            width=width,
            height=height,
            start_cell=route[-1],
            end_cell=end_cell,
            no_fly_cells=no_fly_cells,
        )
        route.extend(tail_path[1:])

    return route


def _plan_time_optimal_open_route(
    width: int,
    height: int,
    start_cell: str,
    no_fly_cells: set[str],
    landing_profile: LandingProfile,
) -> list[str]:
    candidate_end_cells = sorted(
        terminal_cells_for_landing(
            width=width,
            height=height,
            no_fly_cells=no_fly_cells,
            landing_profile=landing_profile,
        )
    )
    if not candidate_end_cells:
        return _plan_legacy_route(
            width=width,
            height=height,
            start_cell=start_cell,
            no_fly_cells=no_fly_cells,
        )

    candidate_routes: list[list[str]] = []
    for end_cell in candidate_end_cells:
        try:
            candidate_route = _plan_legacy_route(
                width=width,
                height=height,
                start_cell=start_cell,
                no_fly_cells=no_fly_cells,
                end_cell=end_cell,
            )
        except ValueError:
            continue
        if candidate_route[-1] == end_cell:
            candidate_routes.append(candidate_route)

    if not candidate_routes:
        return _plan_legacy_route(
            width=width,
            height=height,
            start_cell=start_cell,
            no_fly_cells=no_fly_cells,
        )

    return min(
        candidate_routes,
        key=lambda route: estimate_route_cost(
            route,
            height=height,
            landing_profile=landing_profile,
            width=width,
            no_fly_cells=no_fly_cells,
        ),
    )


def plan_route(
    width: int,
    height: int,
    start_cell: str,
    no_fly_cells: set[str],
    end_cell: str | None = None,
    require_cycle: bool = False,
    mission_mode: str = "legacy",
    landing_profile: LandingProfile | None = None,
) -> list[str]:
    if mission_mode == "time_optimal_open":
        if landing_profile is None:
            raise ValueError("landing_profile is required when mission_mode is time_optimal_open")
        return _plan_time_optimal_open_route(
            width=width,
            height=height,
            start_cell=start_cell,
            no_fly_cells=no_fly_cells,
            landing_profile=landing_profile,
        )

    return _plan_legacy_route(
        width=width,
        height=height,
        start_cell=start_cell,
        no_fly_cells=no_fly_cells,
        end_cell=end_cell,
        require_cycle=require_cycle,
    )
