import asyncio
import json
import logging
import os
import time
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .airborne import AirborneClient, GroundControlCommand
from .config import GatewayConfig
from .models import AckSnapshot, PlanningRequest, WebEvent
from .planner import PlannerClient, PlannerError, is_canonical_plan, store_plan_atomic
from .recorder import JsonlRecorder
from .state import GroundState
from .video import VideoGateway, VideoGatewayError


logger = logging.getLogger(__name__)

_BACKGROUND_RETRY_INITIAL_S = 0.25
_BACKGROUND_RETRY_MAX_S = 5.0


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
    video: VideoGateway | None = None
    stop: asyncio.Event = field(default_factory=asyncio.Event)
    tasks: list[asyncio.Task[Any]] = field(default_factory=list)
    initial_probe_task: asyncio.Task[Any] | None = None
    mission_operation_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def __post_init__(self) -> None:
        path = self.config.runtime_dir / "active_mission_plan.json"
        try:
            plan = json.loads(path.read_text(encoding="utf-8"))
            if is_canonical_plan(plan):
                self.state.apply_plan(plan, _now_ms(), publish=False)
                self.state.mark_resyncing()
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            pass


async def _run_background(coro, stop: asyncio.Event) -> None:
    retry_delay = _BACKGROUND_RETRY_INITIAL_S
    while not stop.is_set():
        try:
            await coro(stop)
            return
        except asyncio.CancelledError:
            raise
        except Exception:
            logger.exception(
                "Background worker failed; retrying in %.2f seconds", retry_delay
            )

        try:
            await asyncio.wait_for(stop.wait(), timeout=retry_delay)
            return
        except asyncio.TimeoutError:
            retry_delay = min(retry_delay * 2, _BACKGROUND_RETRY_MAX_S)


async def _initial_probe(services: GatewayServices) -> None:
    try:
        ack = await services.airborne.probe_once()
        if ack.ok:
            services.state.apply_ack(ack, _now_ms())
    except asyncio.CancelledError:
        raise
    except Exception as error:
        services.state.set_recent_error(f"airborne probe failed: {error}")


async def _heartbeat_after_probe(services: GatewayServices) -> None:
    probe = services.initial_probe_task
    if probe is not None:
        await asyncio.gather(probe, return_exceptions=True)
    if not services.stop.is_set():
        await services.airborne.heartbeat_loop(services.stop)


@asynccontextmanager
async def _lifespan(app: FastAPI):
    services: GatewayServices = app.state.services
    await services.recorder.start()
    services.state.mark_resyncing()
    services.stop.clear()
    services.initial_probe_task = asyncio.create_task(_initial_probe(services))
    services.tasks = [
        asyncio.create_task(_heartbeat_after_probe(services)),
        asyncio.create_task(
            _run_background(services.airborne.telemetry_loop, services.stop)
        ),
    ]
    try:
        yield
    finally:
        services.stop.set()
        tasks = list(services.tasks)
        if services.initial_probe_task is not None:
            tasks.append(services.initial_probe_task)
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        services.tasks.clear()
        services.initial_probe_task = None
        await services.recorder.stop()
        services.airborne.close()
        if services.video is not None:
            await services.video.aclose()


def _error(status: int, code: str, message: str) -> JSONResponse:
    return JSONResponse(
        status_code=status,
        content={"ok": False, "error_code": code, "message": message},
    )


def _ack_response(ack: AckSnapshot) -> JSONResponse:
    return JSONResponse(
        content={"ok": ack.ok, "message": ack.message, "ack": ack.model_dump()}
    )


def create_app(services: GatewayServices | None = None) -> FastAPI:
    if services is None:
        config = GatewayConfig.from_env(os.environ)
        state = GroundState()
        recorder = JsonlRecorder(config.runtime_dir / "sessions", state)
        planner = PlannerClient(
            config.planner_cli, timeout_s=10.0, max_output_bytes=4 * 1024 * 1024
        )
        airborne = AirborneClient(config, state, recorder)
        video = VideoGateway(config)
        services = GatewayServices(config, planner, state, recorder, airborne, video)

    app = FastAPI(lifespan=_lifespan)
    app.state.services = services

    @app.get("/api/health")
    async def health():
        return {"ok": True}

    @app.get("/api/video/health")
    async def video_health():
        video = getattr(services, "video", None)
        available = video is not None and await video.health()
        return {"ok": True, "available": available}

    @app.post("/api/video/whep")
    async def open_video(request: Request):
        if request.headers.get("content-type", "").split(";", 1)[0].strip() != "application/sdp":
            return _error(415, "invalid_video_offer", "video offer must be application/sdp")
        offer = await request.body()
        if not offer or len(offer) > 64 * 1024:
            return _error(413, "invalid_video_offer", "video offer has an invalid size")
        try:
            offer_sdp = offer.decode("utf-8")
        except UnicodeDecodeError:
            return _error(422, "invalid_video_offer", "video offer must be UTF-8 SDP")
        video = getattr(services, "video", None)
        if video is None:
            return _error(503, "video_unavailable", "video gateway is unavailable")
        try:
            session = await video.open(offer_sdp)
        except VideoGatewayError as error:
            status = 504 if error.error_code == "video_upstream_timeout" else 503
            return _error(status, error.error_code, str(error))
        return Response(
            content=session.answer_sdp,
            status_code=201,
            media_type="application/sdp",
            headers={"Location": f"/api/video/whep/{session.session_id}"},
        )

    @app.delete("/api/video/whep/{session_id}", status_code=204)
    async def close_video(session_id: str):
        video = getattr(services, "video", None)
        if video is not None:
            await video.close(session_id)
        return Response(status_code=204)

    @app.get("/api/snapshot")
    async def snapshot():
        return services.state.snapshot(_now_ms()).model_dump(mode="json")

    @app.get("/api/detections/history")
    async def detection_history(task_id: str | None = None):
        """Return detections observed during this Gateway process lifetime."""
        return {
            "ok": True,
            "detections": services.state.detection_history(task_id),
        }

    @app.post("/api/mission/plan")
    async def plan(request: PlanRequest):
        before = services.state.snapshot(_now_ms())
        if before.command_link != "online":
            return _error(409, "command_link_unavailable", "command link is not online")
        if before.airborne_mission_running:
            return _error(409, "mission_running", "cannot replace a running mission")
        repo_root = Path(__file__).resolve().parents[3]
        cases_root = (repo_root / "shared" / "cases").resolve()
        candidate = Path(request.case_path)
        if candidate.is_absolute():
            return _error(422, "invalid_request", "case_path must be relative")
        try:
            resolved = (repo_root / candidate).resolve(strict=False)
            resolved.relative_to(cases_root)
        except ValueError:
            return _error(422, "invalid_request", "case_path escapes cases root")
        try:
            result = await services.planner.plan(
                PlanningRequest(str(resolved), request.no_fly_cells)
            )
            # Planning may take seconds, so only serialize the final recheck and
            # state transition with LOAD/START operations.
            async with services.mission_operation_lock:
                after = services.state.snapshot(_now_ms())
                if after.command_link != "online" or after.airborne_mission_running:
                    return _error(
                        409,
                        "mission_state_changed",
                        "mission state changed while planning; plan was not applied",
                    )
                store_plan_atomic(
                    result, services.config.runtime_dir / "active_mission_plan.json"
                )
                services.state.apply_plan(result, _now_ms())
                services.recorder.record(
                    WebEvent(
                        type="task_plan",
                        seq=services.state.snapshot(_now_ms()).snapshot_seq,
                        timestamp_ms=_now_ms(),
                        task_id=result["task_id"],
                        payload=result,
                    )
                )
            return {"ok": True, "plan": result}
        except PlannerError as error:
            status = (
                422
                if error.error_code in {"invalid_request", "unsafe_path"}
                else 409
                if error.error_code
                in {
                    "invalid_no_fly_zone",
                    "planner_rejected",
                    "case_load_failed",
                    "planning_failed",
                }
                else 504
                if error.error_code == "planner_timeout"
                else 502
            )
            return _error(status, error.error_code, str(error))

    @app.post("/api/mission/load")
    async def load():
        async with services.mission_operation_lock:
            current = services.state.snapshot(_now_ms())
            if current.command_link != "online":
                return _error(
                    409, "command_link_unavailable", "command link is not online"
                )
            if current.airborne_mission_running:
                return _error(
                    409, "mission_running", "cannot load while a mission is running"
                )
            if not current.plan or not current.active_task_id:
                return _error(409, "mission_not_ready", "no active mission plan")
            ack = await services.airborne.send_mission_load(current.plan)
            services.state.apply_ack(ack, _now_ms())
            return (
                _ack_response(ack)
                if ack.ok
                else _error(409, "mission_load_failed", ack.message)
            )

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
        async with services.mission_operation_lock:
            current, failure = _current_ready()
            if failure is not None:
                return failure
            ack = await services.airborne.send_control(
                GroundControlCommand.START, current.active_task_id
            )
            services.state.apply_ack(ack, _now_ms())
            return (
                _ack_response(ack)
                if ack.ok
                else _error(409, "mission_start_failed", ack.message)
            )

    @app.post("/api/mission/stop")
    async def stop():
        current = services.state.snapshot(_now_ms())
        if (
            current.command_link != "online"
            or not current.airborne_mission_running
            or not current.airborne_task_id
        ):
            return _error(409, "mission_not_running", "mission is not running")
        ack = await services.airborne.send_control(
            GroundControlCommand.STOP, current.airborne_task_id
        )
        services.state.apply_ack(ack, _now_ms())
        return (
            _ack_response(ack)
            if ack.ok
            else _error(409, "mission_stop_failed", ack.message)
        )

    @app.post("/api/link/probe")
    async def probe():
        try:
            ack = await services.airborne.probe_once()
        except Exception as error:
            return _error(504, "command_timeout", str(error))
        services.state.apply_ack(ack, _now_ms())
        return (
            _ack_response(ack)
            if ack.ok
            else _error(504, "command_timeout", ack.message)
        )

    @app.websocket("/ws/telemetry")
    async def telemetry(websocket: WebSocket):
        await websocket.accept()
        queue = services.state.subscribe()
        try:
            snapshot = services.state.snapshot(_now_ms()).model_dump(mode="json")
            await websocket.send_json({"type": "snapshot", "snapshot": snapshot})
            while True:
                event_task = asyncio.create_task(queue.get())
                receive_task = asyncio.create_task(websocket.receive())
                try:
                    done, _ = await asyncio.wait(
                        {event_task, receive_task}, return_when=asyncio.FIRST_COMPLETED
                    )
                    if event_task in done:
                        event = event_task.result()
                        queue.task_done()
                        await websocket.send_json(event.model_dump(mode="json"))
                    if receive_task in done:
                        message = receive_task.result()
                        if message.get("type") == "websocket.disconnect":
                            break
                finally:
                    for task in (event_task, receive_task):
                        if not task.done():
                            task.cancel()
                    await asyncio.gather(
                        event_task, receive_task, return_exceptions=True
                    )
        except (WebSocketDisconnect, asyncio.CancelledError):
            pass
        finally:
            services.state.unsubscribe(queue)

    frontend = Path(__file__).resolve().parents[2] / "frontend" / "dist"
    if frontend.is_dir():
        app.mount("/", StaticFiles(directory=frontend, html=True), name="frontend")
    return app
