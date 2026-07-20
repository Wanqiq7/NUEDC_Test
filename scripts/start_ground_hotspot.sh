#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONNECTION_NAME="NUEDC-Ground-Hotspot"
SSID="${NUEDC_HOTSPOT_SSID:-NUEDC-Ground}"
PASSWORD="${NUEDC_HOTSPOT_PASSWORD:-}"
WIFI_IFACE="${NUEDC_WIFI_IFACE:-}"
GROUND_CIDR="${NUEDC_GROUND_HOST:-10.42.0.1/24}"
AIRBORNE_HOST="${NUEDC_AIRBORNE_HOST:-10.42.0.2}"
TELEMETRY_PORT="${NUEDC_TELEMETRY_PORT:-5557}"
COMMAND_PORT="${NUEDC_COMMAND_PORT:-5558}"
RUNTIME_DIR="${NUEDC_RUNTIME_DIR:-${ROOT_DIR}/runtime}"
LAUNCH_APP=0

usage() {
  cat <<'EOF'
Usage:
  scripts/start_ground_hotspot.sh [--iface WLAN_IFACE] [--ssid SSID] [--password PASSWORD]
                                  [--ground-host IPV4/CIDR] [--airborne-host IPV4]
                                  [--launch-app]

Create or update the ground-station Wi-Fi hotspot and write the ground-control
network environment file. The hotspot reconnects automatically after reboot.
EOF
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

detect_wifi_iface() {
  nmcli -t -f DEVICE,TYPE device status | awk -F: '$2 == "wifi" { print $1; exit }'
}

connection_exists() {
  nmcli -t -f NAME connection show | awk -v name="${CONNECTION_NAME}" '$0 == name { found = 1 } END { exit !found }'
}

validate_password() {
  if [[ ${#PASSWORD} -lt 8 || "${PASSWORD}" == *$'\n'* || "${PASSWORD}" == *$'\r'* ]]; then
    echo "Wi-Fi password must be at least eight characters and contain no line breaks." >&2
    exit 1
  fi
}

verify_address() {
  if ! ip -4 -o addr show dev "${WIFI_IFACE}" | awk '$3 == "inet" { print $4 }' | grep -F -x "${GROUND_CIDR}" >/dev/null; then
    echo "Hotspot did not configure ${GROUND_CIDR} on ${WIFI_IFACE}." >&2
    exit 1
  fi
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
    --ground-host)
      GROUND_CIDR="$2"
      shift 2
      ;;
    --airborne-host)
      AIRBORNE_HOST="$2"
      shift 2
      ;;
    --launch-app)
      LAUNCH_APP=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

require_command nmcli
require_command ip
validate_password

if [[ -z "${WIFI_IFACE}" ]]; then
  WIFI_IFACE="$(detect_wifi_iface)"
fi
if [[ -z "${WIFI_IFACE}" ]]; then
  echo "No Wi-Fi interface found; use --iface to select one." >&2
  exit 1
fi

if ! connection_exists; then
  nmcli connection add type wifi ifname "${WIFI_IFACE}" con-name "${CONNECTION_NAME}" autoconnect yes ssid "${SSID}"
fi

nmcli connection modify "${CONNECTION_NAME}" connection.interface-name "${WIFI_IFACE}" connection.autoconnect yes connection.autoconnect-retries 0
nmcli connection modify "${CONNECTION_NAME}" 802-11-wireless.mode ap
nmcli connection modify "${CONNECTION_NAME}" 802-11-wireless.ssid "${SSID}"
nmcli connection modify "${CONNECTION_NAME}" wifi-sec.key-mgmt wpa-psk
nmcli connection modify "${CONNECTION_NAME}" wifi-sec.psk "${PASSWORD}"
nmcli connection modify "${CONNECTION_NAME}" ipv4.method shared ipv4.addresses "${GROUND_CIDR}"
nmcli connection modify "${CONNECTION_NAME}" ipv6.method disabled
nmcli connection up "${CONNECTION_NAME}" ifname "${WIFI_IFACE}"

verify_address

mkdir -p "${RUNTIME_DIR}"
ENV_FILE="${RUNTIME_DIR}/ground_control_network.env"
TEMP_ENV_FILE="${ENV_FILE}.tmp.$$"
trap 'rm -f "${TEMP_ENV_FILE}"' EXIT
cat > "${TEMP_ENV_FILE}" <<EOF
export NUEDC_AIRBORNE_HOST=${AIRBORNE_HOST}
export NUEDC_TELEMETRY_PORT=${TELEMETRY_PORT}
export NUEDC_COMMAND_PORT=${COMMAND_PORT}
EOF
mv "${TEMP_ENV_FILE}" "${ENV_FILE}"
trap - EXIT

echo "Ground hotspot configured"
echo "  Connection: ${CONNECTION_NAME}"
echo "  SSID: ${SSID}"
echo "  Wi-Fi interface: ${WIFI_IFACE}"
echo "  Ground address: ${GROUND_CIDR}"
echo "  Airborne address: ${AIRBORNE_HOST}"
echo "  Environment file: ${ENV_FILE}"

if [[ ${LAUNCH_APP} -eq 1 ]]; then
  # shellcheck disable=SC1090
  source "${ENV_FILE}"
  exec "${ROOT_DIR}/web_ground_station/scripts/start_competition.sh"
fi
