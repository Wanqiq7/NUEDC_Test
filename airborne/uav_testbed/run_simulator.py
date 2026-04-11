from __future__ import annotations

import argparse
import json
import threading
import time
from pathlib import Path

from .case_loader import load_case
from .command_server import CommandServerState, serve_command_endpoint
from .mission_planning import MissionPlan
from .publisher import ZmqPublisher
from .simulator import simulate_messages


def main() -> int:
    parser = argparse.ArgumentParser(description="H 题机载模拟端")
    parser.add_argument("--case", default="shared/cases/sample_case.json", help="案例文件路径")
    parser.add_argument(
        "--mission-plan",
        default="runtime/active_mission_plan.json",
        help="预计算的任务计划 JSON 文件路径",
    )
    parser.add_argument(
        "--command-endpoint",
        default="tcp://0.0.0.0:5558",
        help="任务接收 REP 地址；为空时不启动命令服务",
    )
    parser.add_argument(
        "--wait-for-start",
        action="store_true",
        help="等待 START_MISSION 控制命令后再开始发送任务消息",
    )
    parser.add_argument("--endpoint", default="tcp://0.0.0.0:5557", help="ZeroMQ 发布地址")
    parser.add_argument("--startup-delay", type=float, default=0.4, help="启动后首次发送前延时")
    parser.add_argument("--sleep-scale", type=float, default=1.0, help="tick 时间缩放")
    args = parser.parse_args()
    if args.wait_for_start and not args.command_endpoint:
        parser.error("--wait-for-start requires --command-endpoint")

    case = load_case(args.case)
    mission_plan: MissionPlan | None = None
    plan_path = Path(args.mission_plan)
    command_state = CommandServerState()
    if args.command_endpoint:
        command_thread = threading.Thread(
            target=serve_command_endpoint,
            args=(args.command_endpoint, plan_path),
            kwargs={"server_state": command_state},
            daemon=True,
        )
        command_thread.start()
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
        if args.wait_for_start:
            while not command_state.start_requested.is_set():
                time.sleep(0.05)

        for sequence, message in enumerate(simulate_messages(case, mission_plan=mission_plan), start=1):
            if command_state.stop_requested.is_set():
                break
            publisher.publish(sequence, message)
            if message["message_type"] == "telemetry":
                delay = (case.tick_interval_ms / 1000.0) * args.sleep_scale
                time.sleep(delay)
        return 0
    finally:
        publisher.close()


if __name__ == "__main__":
    raise SystemExit(main())
