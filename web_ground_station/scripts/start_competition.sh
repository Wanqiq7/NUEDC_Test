#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
usage() { echo "用法: web_ground_station/scripts/start_competition.sh [--help]"; }
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then usage; exit 0; fi
[[ $# -eq 0 ]] || { usage >&2; exit 2; }
ENV_FILE="${ROOT_DIR}/runtime/web_ground_station.env"
set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a
"${NUEDC_NETWORK_CHECK:-${ROOT_DIR}/scripts/check_ground_control_network.sh}" \
  --host "${NUEDC_AIRBORNE_HOST}" --telemetry-port "${NUEDC_TELEMETRY_PORT}" \
  --command-port "${NUEDC_COMMAND_PORT}"
"${ROOT_DIR}/web_ground_station/scripts/check_web_ground_station.sh"
cd "${ROOT_DIR}/web_ground_station"
exec uv run uvicorn nuedc_web_gateway.app:create_app --factory \
  --host "${NUEDC_WEB_HOST}" --port "${NUEDC_WEB_PORT}"
