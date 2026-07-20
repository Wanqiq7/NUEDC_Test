#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}/web_ground_station"
uv run python scripts/generate_python_proto.py
uv run uvicorn nuedc_web_gateway.app:create_app --factory --reload --host "${NUEDC_WEB_HOST:-127.0.0.1}" --port "${NUEDC_WEB_PORT:-8000}" &
GATEWAY_PID=$!
trap 'kill "${GATEWAY_PID}" 2>/dev/null || true' EXIT INT TERM
corepack pnpm --dir frontend dev --host 127.0.0.1
