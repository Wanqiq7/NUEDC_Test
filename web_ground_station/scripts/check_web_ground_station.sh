#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENV_FILE="${NUEDC_WEB_ENV_FILE:-${ROOT_DIR}/runtime/web_ground_station.env}"
[[ -f "${ENV_FILE}" ]] || { echo "缺少 ${ENV_FILE}" >&2; exit 1; }
set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a

: "${NUEDC_DEPLOYMENT_MANIFEST:?NUEDC_DEPLOYMENT_MANIFEST is required}"
[[ "${NUEDC_DEPLOYMENT_MANIFEST}" = /* ]] || \
  NUEDC_DEPLOYMENT_MANIFEST="${ROOT_DIR}/${NUEDC_DEPLOYMENT_MANIFEST}"
python3 "${ROOT_DIR}/web_ground_station/scripts/deployment_manifest.py" verify \
  --manifest "${NUEDC_DEPLOYMENT_MANIFEST}" \
  --repo "${ROOT_DIR}" --role ground \
  --proto "${ROOT_DIR}/shared/proto/messages.proto"

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
PY_PROTO="${ROOT_DIR}/web_ground_station/.generated/proto/messages_pb2.py"
PY_PROTO_HASH="${ROOT_DIR}/web_ground_station/.generated/proto/messages.proto.sha256"
PROTO_SOURCE="${ROOT_DIR}/shared/proto/messages.proto"
UVICORN_BIN="${NUEDC_UVICORN_BIN:-${ROOT_DIR}/web_ground_station/.venv/bin/uvicorn}"
[[ -f "${PY_PROTO}" && -f "${PY_PROTO_HASH}" ]] || {
  echo "缺少预生成 Python Protobuf" >&2; exit 1;
}
python3 - "${PROTO_SOURCE}" "${PY_PROTO_HASH}" <<'PY'
import hashlib
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
digest = pathlib.Path(sys.argv[2])
if digest.read_text(encoding="ascii").strip() != hashlib.sha256(source.read_bytes()).hexdigest():
    raise SystemExit("预生成 Python Protobuf 哈希不匹配")
PY
[[ -x "${UVICORN_BIN}" ]] || { echo "Uvicorn 不可执行: ${UVICORN_BIN}" >&2; exit 1; }
FRONTEND_DIST_DIR="${NUEDC_FRONTEND_DIST_DIR:-${ROOT_DIR}/web_ground_station/frontend/dist}"
[[ -f "${FRONTEND_DIST_DIR}/index.html" ]] || { echo "缺少 frontend/dist" >&2; exit 1; }
RUNTIME_DIR="${NUEDC_RUNTIME_DIR:-runtime}"
[[ "${RUNTIME_DIR}" = /* ]] || RUNTIME_DIR="${ROOT_DIR}/${RUNTIME_DIR}"
[[ -d "${RUNTIME_DIR}" ]] || { echo "缺少运行目录: ${RUNTIME_DIR}" >&2; exit 1; }

PLANNER="${NUEDC_PLANNER_CLI:-build/shared/cpp/h_route_planner_cli}"
[[ "${PLANNER}" = /* ]] || PLANNER="${ROOT_DIR}/${PLANNER}"
[[ -x "${PLANNER}" ]] || { echo "规划器不可执行: ${PLANNER}" >&2; exit 1; }
echo "Web 地面站离线预检通过"
