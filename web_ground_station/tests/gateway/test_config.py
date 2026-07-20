from dataclasses import FrozenInstanceError
from pathlib import Path

import pytest

from nuedc_web_gateway.config import GatewayConfig


def test_competition_defaults():
    config = GatewayConfig.from_env({})

    assert config.airborne_host == "10.42.0.2"
    assert config.telemetry_endpoint == "tcp://10.42.0.2:5557"
    assert config.command_endpoint == "tcp://10.42.0.2:5558"
    assert config.web_host == "0.0.0.0"
    assert config.web_port == 8000
    assert config.pid_debug_enabled is False


def test_environment_overrides_all_fields():
    config = GatewayConfig.from_env(
        {
            "NUEDC_AIRBORNE_HOST": "192.0.2.10",
            "NUEDC_TELEMETRY_PORT": "15557",
            "NUEDC_COMMAND_PORT": "15558",
            "NUEDC_PID_DEBUG_ENABLED": "YES",
            "NUEDC_PID_DEBUG_PORT": "15559",
            "NUEDC_WEB_HOST": "127.0.0.1",
            "NUEDC_WEB_PORT": "18000",
            "NUEDC_RUNTIME_DIR": "/tmp/nuedc-runtime",
            "NUEDC_PLANNER_CLI": "/tmp/h-route-planner",
        }
    )

    assert config.telemetry_endpoint == "tcp://192.0.2.10:15557"
    assert config.command_endpoint == "tcp://192.0.2.10:15558"
    assert config.pid_debug_enabled is True
    assert config.pid_debug_port == 15559
    assert config.web_host == "127.0.0.1"
    assert config.web_port == 18000
    assert config.runtime_dir == Path("/tmp/nuedc-runtime")
    assert config.planner_cli == Path("/tmp/h-route-planner")


@pytest.mark.parametrize("value", ["1", "true", "TRUE", "yes", "on", " On "])
def test_pid_debug_true_values(value):
    assert GatewayConfig.from_env({"NUEDC_PID_DEBUG_ENABLED": value}).pid_debug_enabled


@pytest.mark.parametrize("value", ["0", "false", "no", "off", "anything"])
def test_other_pid_debug_values_are_false(value):
    assert not GatewayConfig.from_env({"NUEDC_PID_DEBUG_ENABLED": value}).pid_debug_enabled


@pytest.mark.parametrize(
    ("name", "value"),
    [
        ("NUEDC_TELEMETRY_PORT", "0"),
        ("NUEDC_COMMAND_PORT", "65536"),
        ("NUEDC_PID_DEBUG_PORT", "not-a-port"),
        ("NUEDC_WEB_PORT", "-1"),
    ],
)
def test_invalid_ports_are_rejected(name, value):
    with pytest.raises(ValueError, match=name):
        GatewayConfig.from_env({name: value})


def test_config_is_immutable():
    config = GatewayConfig.from_env({})

    with pytest.raises(FrozenInstanceError):
        config.web_port = 9000
