from dataclasses import dataclass
import math
from pathlib import Path
from typing import Mapping
from urllib.parse import urlsplit


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


def _positive_float(
    env: Mapping[str, str], name: str, default: float
) -> float:
    raw_value = env.get(name, str(default))
    try:
        value = float(raw_value)
    except ValueError as error:
        raise ValueError(f"{name} must be a finite positive number") from error
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be a finite positive number")
    return value


def _loopback_http_url(
    env: Mapping[str, str], name: str, default: str
) -> str:
    value = env.get(name, default).strip()
    parsed = urlsplit(value)
    if (
        parsed.scheme != "http"
        or parsed.hostname not in {"127.0.0.1", "localhost", "::1"}
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError(f"{name} must be a loopback HTTP URL")
    try:
        if parsed.port is None:
            raise ValueError
    except ValueError as error:
        raise ValueError(f"{name} must include a valid port") from error
    return value.rstrip("/")


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
    mediamtx_whep_url: str = "http://127.0.0.1:8889/camera_raw/whep"
    mediamtx_api_url: str = "http://127.0.0.1:9997"
    video_proxy_timeout_s: float = 3.0

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
            mediamtx_whep_url=_loopback_http_url(
                env,
                "NUEDC_MEDIAMTX_WHEP_URL",
                "http://127.0.0.1:8889/camera_raw/whep",
            ),
            mediamtx_api_url=_loopback_http_url(
                env,
                "NUEDC_MEDIAMTX_API_URL",
                "http://127.0.0.1:9997",
            ),
            video_proxy_timeout_s=_positive_float(
                env, "NUEDC_VIDEO_PROXY_TIMEOUT_S", 3.0
            ),
        )

    @property
    def telemetry_endpoint(self) -> str:
        return f"tcp://{self.airborne_host}:{self.telemetry_port}"

    @property
    def command_endpoint(self) -> str:
        return f"tcp://{self.airborne_host}:{self.command_port}"
