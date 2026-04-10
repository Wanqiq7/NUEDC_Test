from __future__ import annotations

import argparse
import json

from .case_loader import load_case
from .mission_planning import build_mission_plan


def main() -> int:
    parser = argparse.ArgumentParser(description="导出任务规划 JSON")
    parser.add_argument("--case", default="cases/sample_case.json", help="案例文件路径")
    parser.add_argument(
        "--no-fly-cells",
        nargs="*",
        dest="no_fly_cells",
        help="覆盖的禁飞格列表",
        default=None,
    )
    args = parser.parse_args()

    case = load_case(args.case)
    if args.no_fly_cells is None:
        override_no_fly_cells = None
    else:
        override_no_fly_cells = tuple(args.no_fly_cells)
    plan = build_mission_plan(case, override_no_fly_cells=override_no_fly_cells)
    print(json.dumps(plan, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
