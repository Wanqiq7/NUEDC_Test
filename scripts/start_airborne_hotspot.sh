#!/usr/bin/env bash
set -euo pipefail

# 该脚本用于在机载端 NUC 上一键开启热点，并打印后续双机联调命令。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSID="${NUEDC_HOTSPOT_SSID:-NUEDC-Airborne}"
PASSWORD="${NUEDC_HOTSPOT_PASSWORD:-12345678}"
WIFI_IFACE="${NUEDC_WIFI_IFACE:-}"
TELEMETRY_PORT="${NUEDC_TELEMETRY_PORT:-5557}"
COMMAND_PORT="${NUEDC_COMMAND_PORT:-5558}"

usage() {
  cat <<'EOF'
用法:
  scripts/start_airborne_hotspot.sh [--iface WLAN_IFACE] [--ssid SSID] [--password PASSWORD]

环境变量:
  NUEDC_WIFI_IFACE
  NUEDC_HOTSPOT_SSID
  NUEDC_HOTSPOT_PASSWORD
  NUEDC_TELEMETRY_PORT
  NUEDC_COMMAND_PORT
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
require_command ip

if [[ -z "${WIFI_IFACE}" ]]; then
  WIFI_IFACE="$(detect_wifi_iface)"
fi

if [[ -z "${WIFI_IFACE}" ]]; then
  echo "未检测到可用的 Wi-Fi 网卡，请通过 --iface 指定。" >&2
  exit 1
fi

nmcli radio wifi on >/dev/null 2>&1 || true
nmcli dev wifi hotspot ifname "${WIFI_IFACE}" ssid "${SSID}" password "${PASSWORD}" >/dev/null

sleep 1
AIRBORNE_HOST="$(ip -4 addr show "${WIFI_IFACE}" | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1)"
if [[ -z "${AIRBORNE_HOST}" ]]; then
  AIRBORNE_HOST="10.42.0.1"
fi

cat <<EOF
机载端热点已启动
  SSID: ${SSID}
  PASSWORD: ${PASSWORD}
  WIFI IFACE: ${WIFI_IFACE}
  AIRBORNE HOST: ${AIRBORNE_HOST}
  TELEMETRY PORT: ${TELEMETRY_PORT}
  COMMAND PORT: ${COMMAND_PORT}

下一步建议:
1. 在地面站 NUC 上运行:
   ${ROOT_DIR}/scripts/setup_ground_control_network.sh --host ${AIRBORNE_HOST} --ssid "${SSID}" --password "${PASSWORD}"

2. 在机载端 NUC 上启动机载程序:
   cd ${ROOT_DIR}
   PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \\
     --case shared/cases/sample_case.json \\
     --mission-plan runtime/active_mission_plan.json \\
     --endpoint tcp://0.0.0.0:${TELEMETRY_PORT} \\
     --command-endpoint tcp://0.0.0.0:${COMMAND_PORT} \\
     --wait-for-start \\
     --sleep-scale 0.02
EOF
