import asyncio
import json
import time
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .airborne import AirborneClient, GroundControlCommand
from .config import GatewayConfig
from .models import AckSnapshot, PlanningRequest, WebEvent
from .planner import PlannerClient, PlannerError, store_plan_atomic
from .recorder import JsonlRecorder
from .state import GroundState


def _now_ms() -> int:
    return time.time_ns() // 1_000_000


class PlanRequest(BaseModel):
    case_path: str = Field(min_length=1)
    no_fly_cells: list[str] = Field(default_factory=list)


@dataclass
class GatewayServices:
    config: GatewayConfig
    planner: PlannerClient
    state: GroundState
    recorder: JsonlRecorder
    airborne: AirborneClient
    stop: asyncio.Event = field(default_factory=asyncio.Event)
    tasks: list[asyncio.Task[Any]] = field(default_factory=list)

    def __post_init__(self) -> None:
        path = self.config.runtime_dir / "active_mission_plan.json"
        try:
            plan = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(plan, dict) and isinstance(plan.get("task_id"), str):
                self.state.apply_plan(plan, _now_ms(), publish=False)
                self.state.mark_resyncing()
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            pass


async def _run_background(coro, stop: asyncio.Event) -> None:
    try:
        await coro(stop)
    except asyncio.CancelledError:
        raise


@asynccontextmanager
async def _lifespan(app: FastAPI):
    services: GatewayServices = app.state.services
    await services.recorder.start()
    try:
        ack = await services.airborne.probe_once()
        if ack.ok:
            services.state.apply_ack(ack, _now_ms())
    except Exception as error:
        services.state.set_recording_error(f"airborne probe failed: {error}")
    services.stop.clear()
    services.tasks = [
        asyncio.create_task(_run_background(services.airborne.heartbeat_loop, services.stop)),
        asyncio.create_task(_run_background(services.airborne.telemetry_loop, services.stop)),
    ]
    try:
        yield
    finally:
        services.stop.set()
        tasks = list(services.tasks)
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        services.tasks.clear()
        await services.recorder.stop()
        services.airborne.close()


def _error(status: int, code: str, message: str) -> JSONResponse:
    return JSONResponse(
        status_code=status,
        content={"ok": False, "error_code": code, "message": message},
    )


def _ack_response(ack: AckSnapshot) -> JSONResponse:
    return JSONResponse(content={"ok": ack.ok, "message": ack.message, "ack": ack.model_dump()})


def create_app(services: GatewayServices | None = None) -> FastAPI:
    if services is None:
        config = GatewayConfig.from_env({})
        state = GroundState()
        recorder = JsonlRecorder(config.runtime_dir / "sessions", state)
        planner = PlannerClient(config.planner_cli, timeout_s=10.0, max_output_bytes=4 * 1024 * 1024)
        airborne = AirborneClient(config, state, recorder)
        services = GatewayServices(config, planner, state, recorder, airborne)

    app = FastAPI(lifespan=_lifespan)
    app.state.services = services

    @app.get("/api/health")
    async def health():
        return {"ok": True}

    @app.get("/api/snapshot")
    async def snapshot():
        return services.state.snapshot(_now_ms()).model_dump(mode="json")

    @app.post("/api/mission/plan")
    async def plan(request: PlanRequest):
        try:
            result = await services.planner.plan(
                PlanningRequest(request.case_path, request.no_fly_cells)
            )
            store_plan_atomic(result, services.config.runtime_dir / "active_mission_plan.json")
            services.state.apply_plan(result, _now_ms())
            services.recorder.record(WebEvent(type="task_plan", seq=services.state.snapshot(_now_ms()).snapshot_seq, timestamp_ms=_now_ms(), task_id=result["task_id"], payload=result))
            return {"ok": True, "plan": result}
        except PlannerError as error:
            status = 409 if error.error_code in {"invalid_no_fly_zone", "planner_rejected"} else 504 if error.error_code == "planner_timeout" else 502
            return _error(status, error.error_code, str(error))

    @app.post("/api/mission/load")
    async def load():
        current = services.state.snapshot(_now_ms())
        if not current.plan or not current.active_task_id:
            return _error(409, "mission_not_ready", "no active mission plan")
        ack = await services.airborne.send_mission_load(current.plan)
        services.state.apply_ack(ack, _now_ms())
        return _ack_response(ack) if ack.ok else _error(409, "mission_load_failed", ack.message)

    def _current_ready() -> tuple[Any, JSONResponse | None]:
        current = services.state.snapshot(_now_ms())
        if (
            current.command_link != "online"
            or not current.active_task_id
            or not current.mission_loaded
            or current.mission_running
            or current.ack is None
            or current.ack.task_id != current.active_task_id
        ):
            return current, _error(409, "mission_not_ready", "mission is not ready")
        return current, None

    @app.post("/api/mission/start")
    async def start():
        current, failure = _current_ready()
        if failure is not None:
            return failure
        ack = await services.airborne.send_control(GroundControlCommand.START, current.active_task_id)
        services.state.apply_ack(ack, _now_ms())
        return _ack_response(ack) if ack.ok else _error(409, "mission_start_failed", ack.message)

    @app.post("/api/mission/stop")
    async def stop():
        current = services.state.snapshot(_now_ms())
        if current.command_link != "online" or not current.mission_running or not current.active_task_id:
            return _error(409, "mission_not_running", "mission is not running")
        ack = await services.airborne.send_control(GroundControlCommand.STOP, current.active_task_id)
        services.state.apply_ack(ack, _now_ms())
        return _ack_response(ack) if ack.ok else _error(409, "mission_stop_failed", ack.message)

    @app.post("/api/link/probe")
    async def probe():
        try:
            ack = await services.airborne.probe_once()
        except Exception as error:
            return _error(504, "command_timeout", str(error))
        services.state.apply_ack(ack, _now_ms())
        return _ack_response(ack) if ack.ok else _error(504, "command_timeout", ack.message)

    @app.websocket("/ws/telemetry")
    async def telemetry(websocket: WebSocket):
        await websocket.accept()
        queue = services.state.subscribe()
        try:
            snapshot = services.state.snapshot(_now_ms()).model_dump(mode="json")
            await websocket.send_json({"type": "snapshot", "snapshot": snapshot})
            while True:
                event = await queue.get()
                try:
                    await websocket.send_json(event.model_dump(mode="json"))
                finally:
                    queue.task_done()
        except (WebSocketDisconnect, asyncio.CancelledError):
            pass
        finally:
            services.state.unsubscribe(queue)

    frontend = Path(__file__).resolve().parents[2] / "frontend" / "dist"
    if frontend.is_dir():
        app.mount("/", StaticFiles(directory=frontend, html=True), name="frontend")
    return app
