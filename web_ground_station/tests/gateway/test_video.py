import httpx
import pytest

from nuedc_web_gateway.config import GatewayConfig
from nuedc_web_gateway.video import VideoGateway, VideoGatewayError


def config(tmp_path):
    return GatewayConfig(
        "10.42.0.2",
        5557,
        5558,
        False,
        9870,
        "0.0.0.0",
        8000,
        tmp_path,
        tmp_path / "planner",
        "http://127.0.0.1:8889/camera_raw/whep",
        "http://127.0.0.1:9997",
        0.5,
    )


@pytest.mark.asyncio
async def test_open_forwards_sdp_and_close_uses_private_upstream_location(tmp_path):
    requests = []

    async def handler(request):
        requests.append(request)
        if request.method == "POST":
            assert request.url == "http://127.0.0.1:8889/camera_raw/whep"
            assert request.headers["content-type"] == "application/sdp"
            assert await request.aread() == b"v=0\r\noffer"
            return httpx.Response(
                201,
                headers={
                    "Content-Type": "application/sdp",
                    "Location": "/camera_raw/whep/upstream-secret",
                },
                text="v=0\r\nanswer",
            )
        assert request.method == "DELETE"
        assert request.url == (
            "http://127.0.0.1:8889/camera_raw/whep/upstream-secret"
        )
        return httpx.Response(200)

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    gateway = VideoGateway(config(tmp_path), client=client)

    session = await gateway.open("v=0\r\noffer")

    assert session.answer_sdp == "v=0\r\nanswer"
    assert session.session_id != "upstream-secret"
    assert "/" not in session.session_id
    assert await gateway.close(session.session_id) is True
    assert await gateway.close(session.session_id) is False
    assert [request.method for request in requests] == ["POST", "DELETE"]
    await client.aclose()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("response", "error_code"),
    [
        (httpx.Response(503, text="down"), "video_upstream_unavailable"),
        (
            httpx.Response(201, headers={"Content-Type": "text/plain"}, text="answer"),
            "invalid_video_answer",
        ),
        (
            httpx.Response(
                201,
                headers={"Content-Type": "application/sdp"},
                text="answer",
            ),
            "invalid_video_answer",
        ),
        (
            httpx.Response(
                201,
                headers={
                    "Content-Type": "application/sdp",
                    "Location": "http://example.com/session",
                },
                text="answer",
            ),
            "invalid_video_answer",
        ),
    ],
)
async def test_open_rejects_failed_or_untrusted_upstream_response(
    tmp_path, response, error_code
):
    client = httpx.AsyncClient(
        transport=httpx.MockTransport(lambda request: response)
    )
    gateway = VideoGateway(config(tmp_path), client=client)

    with pytest.raises(VideoGatewayError) as captured:
        await gateway.open("offer")

    assert captured.value.error_code == error_code
    await client.aclose()


@pytest.mark.asyncio
async def test_timeout_is_reported_without_exposing_upstream_details(tmp_path):
    def timeout(request):
        raise httpx.ReadTimeout("private upstream detail", request=request)

    client = httpx.AsyncClient(transport=httpx.MockTransport(timeout))
    gateway = VideoGateway(config(tmp_path), client=client)

    with pytest.raises(VideoGatewayError) as captured:
        await gateway.open("offer")

    assert captured.value.error_code == "video_upstream_timeout"
    assert "private upstream detail" not in str(captured.value)
    await client.aclose()


@pytest.mark.asyncio
async def test_health_uses_fixed_loopback_mediamtx_api(tmp_path):
    requested = []

    def handler(request):
        requested.append(str(request.url))
        return httpx.Response(200)

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    gateway = VideoGateway(config(tmp_path), client=client)

    assert await gateway.health() is True
    assert requested == ["http://127.0.0.1:9997/v3/config/global/get"]
    await client.aclose()


@pytest.mark.asyncio
async def test_health_returns_false_on_transport_failure(tmp_path):
    def unavailable(request):
        raise httpx.ConnectError("offline", request=request)

    client = httpx.AsyncClient(transport=httpx.MockTransport(unavailable))
    gateway = VideoGateway(config(tmp_path), client=client)

    assert await gateway.health() is False
    await client.aclose()
