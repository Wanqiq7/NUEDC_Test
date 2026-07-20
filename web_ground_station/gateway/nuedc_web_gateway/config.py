from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


def _port(env: Mapping[str, str], name: str, default: int) -> int:
    raw_value = env.get(name, str(default))
    try:
        value = int(raw_value)
    except ValueError as error:
        raise ValueError(f"{name} must be an integer port") from error
    if not 1 <= value <= 65535:
        raise ValueError(f"{name} must be between 1 and 65535")
    return value


def _boolean(env: Mapping[str, str], name: str, default: bool) -> bool:
    if name not in env:
        return default
    return env[name].strip().lower() in {"1", "true", "yes", "on"}


@dataclass(frozen=True)
class GatewayConfig:
    airborne_host: str
    telemetry_port: int
    command_port: int
    pid_debug_enabled: bool
    pid_debug_port: int
    web_host: str
    web_port: int
    runtime_dir: Path
    planner_cli: Path

    @classmethod
    def from_env(cls, env: Mapping[str, str]) -> "GatewayConfig":
        return cls(
            airborne_host=env.get("NUEDC_AIRBORNE_HOST", "10.42.0.2"),
            telemetry_port=_port(env, "NUEDC_TELEMETRY_PORT", 5557),
            command_port=_port(env, "NUEDC_COMMAND_PORT", 5558),
            pid_debug_enabled=_boolean(env, "NUEDC_PID_DEBUG_ENABLED", False),
            pid_debug_port=_port(env, "NUEDC_PID_DEBUG_PORT", 9870),
            web_host=env.get("NUEDC_WEB_HOST", "0.0.0.0"),
            web_port=_port(env, "NUEDC_WEB_PORT", 8000),
            runtime_dir=Path(env.get("NUEDC_RUNTIME_DIR", "runtime")),
            planner_cli=Path(
                env.get(
                    "NUEDC_PLANNER_CLI",
                    "build/shared/cpp/h_route_planner_cli",
                )
            ),
        )

    @property
    def telemetry_endpoint(self) -> str:
        return f"tcp://{self.airborne_host}:{self.telemetry_port}"

    @property
    def command_endpoint(self) -> str:
        return f"tcp://{self.airborne_host}:{self.command_port}"
