import asyncio
from dataclasses import dataclass
import secrets
from urllib.parse import urljoin, urlsplit

import httpx

from .config import GatewayConfig


class VideoGatewayError(RuntimeError):
    def __init__(self, error_code: str, message: str):
        super().__init__(message)
        self.error_code = error_code


@dataclass(frozen=True)
class VideoSession:
    session_id: str
    answer_sdp: str


class VideoGateway:
    def __init__(
        self,
        config: GatewayConfig,
        client: httpx.AsyncClient | None = None,
    ):
        self._config = config
        self._client = client or httpx.AsyncClient(
            timeout=httpx.Timeout(config.video_proxy_timeout_s),
            follow_redirects=False,
        )
        self._owns_client = client is None
        self._sessions: dict[str, str] = {}
        self._lock = asyncio.Lock()

    def _trusted_session_url(self, location: str) -> str:
        upstream = urlsplit(self._config.mediamtx_whep_url)
        session_url = urljoin(self._config.mediamtx_whep_url + "/", location)
        session = urlsplit(session_url)
        if (
            session.scheme != upstream.scheme
            or session.hostname != upstream.hostname
            or session.port != upstream.port
            or not session.path.startswith(upstream.path.rstrip("/") + "/")
            or session.query
            or session.fragment
        ):
            raise VideoGatewayError(
                "invalid_video_answer", "media gateway returned an invalid session"
            )
        return session_url

    async def open(self, offer_sdp: str) -> VideoSession:
        try:
            response = await self._client.post(
                self._config.mediamtx_whep_url,
                content=offer_sdp.encode("utf-8"),
                headers={
                    "Accept": "application/sdp",
                    "Content-Type": "application/sdp",
                },
            )
        except httpx.TimeoutException as error:
            raise VideoGatewayError(
                "video_upstream_timeout", "media gateway did not respond in time"
            ) from error
        except httpx.HTTPError as error:
            raise VideoGatewayError(
                "video_upstream_unavailable", "media gateway is unavailable"
            ) from error
        if response.status_code != 201:
            raise VideoGatewayError(
                "video_upstream_unavailable", "media gateway rejected the session"
            )
        content_type = response.headers.get("content-type", "").split(";", 1)[0]
        location = response.headers.get("location", "")
        if (
            content_type.strip() != "application/sdp"
            or not response.content
            or len(response.content) > 64 * 1024
            or not location
        ):
            raise VideoGatewayError(
                "invalid_video_answer", "media gateway returned an invalid answer"
            )
        try:
            answer_sdp = response.content.decode("utf-8")
        except UnicodeDecodeError as error:
            raise VideoGatewayError(
                "invalid_video_answer", "media gateway returned an invalid answer"
            ) from error
        upstream_session = self._trusted_session_url(location)
        session_id = secrets.token_urlsafe(18)
        async with self._lock:
            self._sessions[session_id] = upstream_session
        return VideoSession(session_id, answer_sdp)

    async def close(self, session_id: str) -> bool:
        async with self._lock:
            upstream_session = self._sessions.pop(session_id, None)
        if upstream_session is None:
            return False
        try:
            await self._client.delete(upstream_session)
        except httpx.HTTPError:
            pass
        return True

    async def health(self) -> bool:
        try:
            response = await self._client.get(
                self._config.mediamtx_api_url + "/v3/config/global/get"
            )
        except httpx.HTTPError:
            return False
        return response.status_code == 200

    async def aclose(self) -> None:
        async with self._lock:
            session_ids = list(self._sessions)
        for session_id in session_ids:
            await self.close(session_id)
        if self._owns_client:
            await self._client.aclose()
