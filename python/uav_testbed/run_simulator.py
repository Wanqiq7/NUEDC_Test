from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from .case_loader import load_case
from .mission_planning import MissionPlan
from .publisher import ZmqPublisher
from .simulator import simulate_messages


def main() -> int:
    parser = argparse.ArgumentParser(description="H 题机载模拟端")
    parser.add_argument("--case", default="cases/sample_case.json", help="案例文件路径")
    parser.add_argument(
        "--mission-plan",
        default="cases/active_mission_plan.json",
        help="预计算的任务计划 JSON 文件路径",
    )
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5557", help="ZeroMQ 发布地址")
    parser.add_argument("--startup-delay", type=float, default=0.4, help="启动后首次发送前延时")
    parser.add_argument("--sleep-scale", type=float, default=1.0, help="tick 时间缩放")
    args = parser.parse_args()

    case = load_case(args.case)
    mission_plan: MissionPlan | None = None
    plan_path = Path(args.mission_plan)
    if plan_path.is_file():
        try:
            with plan_path.open(encoding="utf-8") as handle:
                mission_plan = json.load(handle)
        except (OSError, json.JSONDecodeError) as exc:
            print(f"无法加载任务计划 {plan_path}: {exc}")
            mission_plan = None
    publisher = ZmqPublisher(args.endpoint)
    time.sleep(args.startup_delay)

    try:
        for sequence, message in enumerate(simulate_messages(case, mission_plan=mission_plan), start=1):
            publisher.publish(sequence, message)
            if message["message_type"] == "telemetry":
                delay = (case.tick_interval_ms / 1000.0) * args.sleep_scale
                time.sleep(delay)
        return 0
    finally:
        publisher.close()


if __name__ == "__main__":
    raise SystemExit(main())
