#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}/web_ground_station"
ENV_FILE="${NUEDC_WEB_ENV_FILE:-${ROOT_DIR}/runtime/web_ground_station.env}"
if [[ -f "${ENV_FILE}" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "${ENV_FILE}"
  set +a
fi
[[ "${NUEDC_RUNTIME_DIR:-}" = /* ]] || NUEDC_RUNTIME_DIR="${ROOT_DIR}/${NUEDC_RUNTIME_DIR:-runtime}"
[[ "${NUEDC_PLANNER_CLI:-}" = /* ]] || NUEDC_PLANNER_CLI="${ROOT_DIR}/${NUEDC_PLANNER_CLI:-build/shared/cpp/h_route_planner_cli}"
export NUEDC_RUNTIME_DIR NUEDC_PLANNER_CLI
uv run python scripts/generate_python_proto.py
PYTHONPATH="${ROOT_DIR}/web_ground_station/gateway${PYTHONPATH:+:${PYTHONPATH}}" \
  uv run uvicorn nuedc_web_gateway.app:create_app --factory --reload \
  --host "${NUEDC_WEB_HOST:-127.0.0.1}" --port "${NUEDC_WEB_PORT:-8000}" &
GATEWAY_PID=$!
trap 'kill "${GATEWAY_PID}" 2>/dev/null || true' EXIT INT TERM
corepack pnpm --dir frontend dev --host 127.0.0.1
