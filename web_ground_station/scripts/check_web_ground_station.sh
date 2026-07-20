#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENV_FILE="${NUEDC_WEB_ENV_FILE:-${ROOT_DIR}/runtime/web_ground_station.env}"
[[ -f "${ENV_FILE}" ]] || { echo "缺少 ${ENV_FILE}" >&2; exit 1; }
set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a

[[ "${NUEDC_AIRBORNE_HOST:-}" == "10.42.0.2" ]] || { echo "机载端必须为 10.42.0.2" >&2; exit 1; }
[[ "${NUEDC_WEB_HOST:-}" == "0.0.0.0" || "${NUEDC_WEB_HOST:-}" == "10.42.0.1" ]] || {
  echo "Web 主机必须兼容 10.42.0.1 热点入口" >&2; exit 1;
}
[[ "${NUEDC_WEB_PORT:-}" == "8000" ]] || { echo "Web 端口必须为 8000" >&2; exit 1; }
for port_name in NUEDC_TELEMETRY_PORT NUEDC_COMMAND_PORT NUEDC_PID_DEBUG_PORT; do
  port_value="${!port_name:-}"
  [[ "${port_value}" =~ ^[0-9]+$ && ${port_value} -ge 1 && ${port_value} -le 65535 ]] || {
    echo "端口配置无效: ${port_name}" >&2; exit 1;
  }
done
[[ -f "${ROOT_DIR}/web_ground_station/uv.lock" ]] || { echo "缺少 uv.lock" >&2; exit 1; }
[[ -f "${ROOT_DIR}/web_ground_station/frontend/pnpm-lock.yaml" ]] || { echo "缺少 pnpm-lock.yaml" >&2; exit 1; }
FRONTEND_DIST_DIR="${NUEDC_FRONTEND_DIST_DIR:-${ROOT_DIR}/web_ground_station/frontend/dist}"
[[ -f "${FRONTEND_DIST_DIR}/index.html" ]] || { echo "缺少 frontend/dist" >&2; exit 1; }
RUNTIME_DIR="${NUEDC_RUNTIME_DIR:-runtime}"
[[ "${RUNTIME_DIR}" = /* ]] || RUNTIME_DIR="${ROOT_DIR}/${RUNTIME_DIR}"
[[ -d "${RUNTIME_DIR}" ]] || { echo "缺少运行目录: ${RUNTIME_DIR}" >&2; exit 1; }

PLANNER="${NUEDC_PLANNER_CLI:-build/shared/cpp/h_route_planner_cli}"
[[ "${PLANNER}" = /* ]] || PLANNER="${ROOT_DIR}/${PLANNER}"
[[ -x "${PLANNER}" ]] || { echo "规划器不可执行: ${PLANNER}" >&2; exit 1; }
echo "Web 地面站离线预检通过"
