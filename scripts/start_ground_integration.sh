#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${NUEDC_NETWORK_ENV_FILE:-${ROOT_DIR}/runtime/ground_control_network.env}"
NETWORK_CHECK="${NUEDC_NETWORK_CHECK_PATH:-${ROOT_DIR}/scripts/check_ground_control_network.sh}"
GROUND_APP="${NUEDC_GROUND_APP_PATH:-${ROOT_DIR}/build/ground_station_computer/ground_station_app}"

usage() {
  cat <<'EOF'
用法:
  scripts/start_ground_integration.sh [-- APP_ARGUMENT...]

加载地面站网络环境，确认机载端遥测和命令端口可用，然后启动地面站。
首次配置热点时请先运行 scripts/start_ground_hotspot.sh。
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi
if [[ ${1:-} == "--" ]]; then
  shift
fi

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "找不到网络环境文件: ${ENV_FILE}" >&2
  echo "请先运行 scripts/start_ground_hotspot.sh 配置地面站热点。" >&2
  exit 1
fi
if [[ ! -x "${NETWORK_CHECK}" ]]; then
  echo "网络检查脚本不可执行: ${NETWORK_CHECK}" >&2
  exit 1
fi
if [[ ! -x "${GROUND_APP}" ]]; then
  echo "地面站程序不存在或不可执行: ${GROUND_APP}" >&2
  echo "请先在 ${ROOT_DIR} 完成 CMake 构建。" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${ENV_FILE}"
: "${NUEDC_AIRBORNE_HOST:?网络环境缺少 NUEDC_AIRBORNE_HOST}"
: "${NUEDC_TELEMETRY_PORT:?网络环境缺少 NUEDC_TELEMETRY_PORT}"
: "${NUEDC_COMMAND_PORT:?网络环境缺少 NUEDC_COMMAND_PORT}"

"${NETWORK_CHECK}" \
  --host "${NUEDC_AIRBORNE_HOST}" \
  --telemetry-port "${NUEDC_TELEMETRY_PORT}" \
  --command-port "${NUEDC_COMMAND_PORT}"

echo "网络预检通过，启动地面站。"
exec "${GROUND_APP}" "$@"
