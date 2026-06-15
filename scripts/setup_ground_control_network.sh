#!/usr/bin/env bash
set -euo pipefail

# 该脚本用于在地面站 NUC 上连接机载热点，并生成地面站环境变量文件。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSID="${NUEDC_HOTSPOT_SSID:-NUEDC-Airborne}"
PASSWORD="${NUEDC_HOTSPOT_PASSWORD:-12345678}"
WIFI_IFACE="${NUEDC_WIFI_IFACE:-}"
AIRBORNE_HOST="${NUEDC_AIRBORNE_HOST:-10.42.0.1}"
TELEMETRY_PORT="${NUEDC_TELEMETRY_PORT:-5557}"
COMMAND_PORT="${NUEDC_COMMAND_PORT:-5558}"
LAUNCH_APP=0
ENV_FILE="${ROOT_DIR}/runtime/ground_control_network.env"

usage() {
  cat <<'EOF'
用法:
  scripts/setup_ground_control_network.sh [--iface WLAN_IFACE] [--ssid SSID] [--password PASSWORD] [--host AIRBORNE_HOST] [--launch-app]

功能:
  1. 连接机载端热点
  2. 生成 runtime/ground_control_network.env
  3. 可选直接启动地面站程序
EOF
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "缺少命令: $1" >&2
    exit 1
  fi
}

detect_wifi_iface() {
  nmcli -t -f DEVICE,TYPE dev status | awk -F: '$2=="wifi"{print $1; exit}'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface)
      WIFI_IFACE="$2"
      shift 2
      ;;
    --ssid)
      SSID="$2"
      shift 2
      ;;
    --password)
      PASSWORD="$2"
      shift 2
      ;;
    --host)
      AIRBORNE_HOST="$2"
      shift 2
      ;;
    --launch-app)
      LAUNCH_APP=1
      shift 1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "未知参数: $1" >&2
      usage
      exit 1
      ;;
  esac
done

require_command nmcli

if [[ -z "${WIFI_IFACE}" ]]; then
  WIFI_IFACE="$(detect_wifi_iface)"
fi

if [[ -z "${WIFI_IFACE}" ]]; then
  echo "未检测到可用的 Wi-Fi 网卡，请通过 --iface 指定。" >&2
  exit 1
fi

nmcli dev wifi connect "${SSID}" password "${PASSWORD}" ifname "${WIFI_IFACE}" >/dev/null

mkdir -p "${ROOT_DIR}/runtime"
cat > "${ENV_FILE}" <<EOF
export NUEDC_AIRBORNE_HOST=${AIRBORNE_HOST}
export NUEDC_TELEMETRY_PORT=${TELEMETRY_PORT}
export NUEDC_COMMAND_PORT=${COMMAND_PORT}
EOF

cat <<EOF
地面站网络已配置完成
  WIFI IFACE: ${WIFI_IFACE}
  SSID: ${SSID}
  AIRBORNE HOST: ${AIRBORNE_HOST}
  TELEMETRY PORT: ${TELEMETRY_PORT}
  COMMAND PORT: ${COMMAND_PORT}
  ENV FILE: ${ENV_FILE}
EOF

if [[ ${LAUNCH_APP} -eq 1 ]]; then
  # 这里使用当前 shell 导出环境，确保地面站进程能读取到目标机载地址。
  # shellcheck disable=SC1090
  source "${ENV_FILE}"
  exec "${ROOT_DIR}/build/ground_station_computer/ground_station_app"
fi

cat <<EOF
后续可执行:
  source "${ENV_FILE}"
  ${ROOT_DIR}/scripts/check_ground_control_network.sh --host ${AIRBORNE_HOST}
  ./build/ground_station_computer/ground_station_app
EOF
