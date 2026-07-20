from types import SimpleNamespace

import pytest

from nuedc_web_gateway import emergency_stop


@pytest.mark.asyncio
async def test_emergency_stop_sends_one_stop_with_requested_task_id(monkeypatch):
    calls = []

    class FakeClient:
        def __init__(self, config, state, recorder):
            pass

        async def send_control(self, command, task_id):
            calls.append((command, task_id))
            return SimpleNamespace(ok=True)

        def close(self):
            pass

    monkeypatch.setattr(emergency_stop, "AirborneClient", FakeClient)
    config = SimpleNamespace(runtime_dir=__import__("pathlib").Path("/tmp"))
    assert await emergency_stop.send_emergency_stop("mission-42", config)
    assert calls == [(emergency_stop.GroundControlCommand.STOP, "mission-42")]
