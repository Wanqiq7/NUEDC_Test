"""Browser-independent emergency STOP command."""

import argparse
import asyncio
import os
from pathlib import Path
from typing import Mapping

from .airborne import AirborneClient, GroundControlCommand
from .config import GatewayConfig
from .recorder import JsonlRecorder
from .state import GroundState


def _load_env(path: Path, base: Mapping[str, str] | None = None) -> dict[str, str]:
    values = dict(os.environ if base is None else base)
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values.setdefault(key.strip(), value.strip().strip('"').strip("'"))
    return values


async def send_emergency_stop(task_id: str, config: GatewayConfig) -> bool:
    state = GroundState()
    recorder = JsonlRecorder(config.runtime_dir / "sessions", state)
    client = AirborneClient(config, state, recorder)
    try:
        ack = await client.send_control(GroundControlCommand.STOP, task_id)
        return bool(ack.ok)
    finally:
        client.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Send one reliable emergency STOP command")
    parser.add_argument("--task-id", required=True, help="active mission task identifier")
    parser.add_argument(
        "--env-file",
        type=Path,
        default=Path("runtime/web_ground_station.env"),
        help="runtime environment file (default: runtime/web_ground_station.env)",
    )
    args = parser.parse_args(argv)
    env_file = args.env_file
    if not env_file.is_absolute():
        env_file = Path(__file__).resolve().parents[3] / env_file
    config = GatewayConfig.from_env(_load_env(env_file))
    try:
        ok = asyncio.run(send_emergency_stop(args.task_id, config))
    except Exception as error:  # CLI must remain usable even when the link is down.
        print(f"紧急停止失败: {error}")
        return 1
    if ok:
        print(f"紧急停止已确认: {args.task_id}")
        return 0
    print(f"紧急停止未确认: {args.task_id}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
