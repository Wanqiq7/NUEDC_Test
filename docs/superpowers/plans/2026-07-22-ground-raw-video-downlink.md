# Ground Raw Video Downlink Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add supervised MediaMTX, a same-origin WHEP proxy, and a reconnecting full-screen raw-camera viewer to the Web ground station.

**Architecture:** `start_competition.sh` supervises a pinned local MediaMTX process. FastAPI exposes bounded WHEP session operations against that fixed local upstream, while a focused Vue composable owns WebRTC lifecycle and a full-screen Quasar dialog owns presentation.

**Tech Stack:** Python 3.10, FastAPI, httpx, MediaMTX, WHEP/WebRTC, Vue 3, Quasar, TypeScript, Vitest, Playwright.

## Global Constraints

- The browser only calls same-origin FastAPI endpoints and never connects directly to the airborne host.
- Video is read-only, raw, silent, on-demand, unrecorded, and independent of mission state.
- Closing the dialog must release the WHEP session; stale video over one second triggers reconnect with a two-second maximum backoff.
- The complete frame must remain visible with `object-fit: contain` at a minimum viewport of `1024x600`.
- Competition startup is offline and uses a pinned native MediaMTX binary, never Docker or online installation.
- Existing ZeroMQ, Protobuf, mission, planning, telemetry, and recording contracts remain unchanged.

---

### Task 1: Video Configuration Contract

**Files:**
- Modify: `web_ground_station/gateway/nuedc_web_gateway/config.py`
- Modify: `web_ground_station/tests/gateway/test_config.py`
- Modify: `runtime/web_ground_station.env.example`

**Interfaces:**
- Produces: `GatewayConfig.mediamtx_whep_url: str`, `video_proxy_timeout_s: float`, and validated MediaMTX paths used by later tasks.

- [ ] Write failing tests for default loopback WHEP URL, environment overrides, and rejection of non-loopback upstream URLs.
- [ ] Run `UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run pytest tests/gateway/test_config.py -q` and verify the new assertions fail.
- [ ] Add immutable configuration fields and strict URL validation; never include RTSP credentials in this object or its representations.
- [ ] Run the focused test and verify it passes.

### Task 2: Bounded WHEP Gateway

**Files:**
- Create: `web_ground_station/gateway/nuedc_web_gateway/video.py`
- Create: `web_ground_station/tests/gateway/test_video.py`
- Modify: `web_ground_station/gateway/nuedc_web_gateway/app.py`
- Modify: `web_ground_station/pyproject.toml`
- Modify: `web_ground_station/uv.lock`

**Interfaces:**
- Consumes: fixed `GatewayConfig.mediamtx_whep_url`.
- Produces: `VideoGateway.open(offer_sdp) -> VideoSession`, `VideoGateway.close(session_id)`, `/api/video/whep`, `/api/video/whep/{session_id}`, and `/api/video/health`.

- [ ] Write failing tests that use `httpx.MockTransport` to verify SDP forwarding, response validation, opaque session mapping, DELETE cleanup, size limits, timeouts, and upstream failure mapping.
- [ ] Run the focused tests and confirm failures are caused by the missing gateway.
- [ ] Implement `VideoGateway` with a fixed upstream, an async lock around session mappings, strict `application/sdp`, a bounded body, and three-second upstream timeout.
- [ ] Add FastAPI routes without passing video bytes through Python; close outstanding upstream sessions during lifespan shutdown.
- [ ] Move `httpx` into runtime dependencies, refresh the lock file, and rerun focused tests.
- [ ] Run all Gateway tests with loopback permission and verify no mission API regression.

### Task 3: MediaMTX Offline Runtime

**Files:**
- Create: `web_ground_station/config/mediamtx.yml`
- Create: `web_ground_station/scripts/run_mediamtx.sh`
- Create: `web_ground_station/scripts/tests/test_run_mediamtx.sh`
- Modify: `web_ground_station/scripts/start_competition.sh`
- Modify: `web_ground_station/scripts/check_web_ground_station.sh`
- Modify: `web_ground_station/tests/test_scripts.sh`
- Modify: `runtime/web_ground_station.env.example`

**Interfaces:**
- Produces: a supervised `camera_raw` MediaMTX path and local WHEP endpoint; consumes RTSP credentials only through environment variables.

- [ ] Write shell tests with a fake MediaMTX executable that cover missing binary, missing credentials, source URL construction without log disclosure, readiness failure, restart, and cleanup.
- [ ] Run `bash web_ground_station/tests/test_scripts.sh` and verify the new cases fail.
- [ ] Add a locked-down config with recording disabled, WHEP on loopback-facing configuration, `camera_raw` source-on-demand, and one-second close delay.
- [ ] Implement the supervisor with bounded restart backoff, traps, readiness checks, and redacted output.
- [ ] Extend competition preflight to require executable binary, config, credentials, and fixed local ports.
- [ ] Run shell tests and existing deployment checks.

### Task 4: Browser WHEP Client

**Files:**
- Create: `web_ground_station/frontend/src/composables/useRawVideo.ts`
- Create: `web_ground_station/frontend/src/composables/useRawVideo.spec.ts`

**Interfaces:**
- Produces: `open(video: HTMLVideoElement)`, `close()`, and reactive state `idle | connecting | live | interrupted`.

- [ ] Write failing Vitest tests with controlled `RTCPeerConnection`, fetch, timers, and `requestVideoFrameCallback` implementations.
- [ ] Cover complete ICE gathering before POST, remote SDP application, one-second stale-frame detection, 250/500/1000/2000 ms reconnect, cancellation, DELETE, peer close, and `srcObject = null`.
- [ ] Run the focused spec and verify expected failures.
- [ ] Implement the smallest WHEP client satisfying the tests, keeping session URLs same-origin and reconnect state local to the composable.
- [ ] Rerun the focused test and then all frontend unit tests.

### Task 5: Full-Screen Video Dialog

**Files:**
- Create: `web_ground_station/frontend/src/components/RawVideoDialog.vue`
- Create: `web_ground_station/frontend/src/components/RawVideoDialog.spec.ts`
- Modify: `web_ground_station/frontend/src/components/StatusBar.vue`
- Modify: `web_ground_station/frontend/src/styles/app.scss`

**Interfaces:**
- Consumes: `useRawVideo`.
- Produces: top-bar `videocam` button and full-screen raw video dialog.

- [ ] Write failing component tests for always-enabled entry, full-screen dialog, muted/autoplay/playsinline video, status labels, complete-frame styling, and cleanup on close/unmount.
- [ ] Run the focused test and verify it fails because the component and button do not exist.
- [ ] Implement an industrial, restrained dialog matching the existing console: black video field, compact top strip, icon-only close action, no nested cards or explanatory copy.
- [ ] Add responsive CSS with stable full-viewport dimensions and `object-fit: contain`.
- [ ] Run focused tests, all Vitest tests, TypeScript checking, and production build.

### Task 6: Functional Browser Verification

**Files:**
- Modify: `web_ground_station/tests/e2e/layout.spec.ts`
- Create: `web_ground_station/tests/e2e/raw-video.spec.ts`

**Interfaces:**
- Consumes: built frontend and mockable WHEP endpoint.
- Produces: repeatable acceptance checks at required viewports.

- [ ] Add Playwright coverage for the camera button and full-screen dialog at `1024x600`, `1366x768`, and `1920x1080`.
- [ ] Add a browser-side fake WebRTC session to verify connecting, live, interrupted, reconnect, and close without needing RK3588 hardware.
- [ ] Run the new test and verify it fails before UI integration.
- [ ] Run Playwright after implementation, capture screenshots, and inspect them for overlap, clipping, crop, and blank video-field layout.
- [ ] Run `ruff`, Gateway tests, Vitest, typecheck, build, script tests, and the complete relevant Playwright suite.

## Verification Commands

```bash
cd web_ground_station
UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run pytest tests/gateway -q
UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run ruff check gateway tests/gateway
bash tests/test_scripts.sh

cd frontend
corepack pnpm test
corepack pnpm typecheck
corepack pnpm build
corepack pnpm playwright test tests/e2e/raw-video.spec.ts tests/e2e/layout.spec.ts
```
