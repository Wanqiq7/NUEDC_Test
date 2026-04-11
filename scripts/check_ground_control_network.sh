#!/usr/bin/env bash
set -euo pipefail

# 该脚本用于在地面站 NUC 上快速检查热点连接、目标主机连通性和端口开放情况。

AIRBORNE_HOST="${NUEDC_AIRBORNE_HOST:-10.42.0.1}"
TELEMETRY_PORT="${NUEDC_TELEMETRY_PORT:-5557}"
COMMAND_PORT="${NUEDC_COMMAND_PORT:-5558}"
PING_COUNT=2

usage() {
  cat <<'EOF'
用法:
  scripts/check_ground_control_network.sh [--host AIRBORNE_HOST] [--telemetry-port PORT] [--command-port PORT]
EOF
}

check_tcp_port() {
  local host="$1"
  local port="$2"
  if command -v nc >/dev/null 2>&1; then
    nc -z -w 2 "$host" "$port"
    return
  fi
  timeout 2 bash -lc "</dev/tcp/${host}/${port}" >/dev/null 2>&1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      AIRBORNE_HOST="$2"
      shift 2
      ;;
    --telemetry-port)
      TELEMETRY_PORT="$2"
      shift 2
      ;;
    --command-port)
      COMMAND_PORT="$2"
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

echo "检查目标机载端: ${AIRBORNE_HOST}"
echo "遥测端口: ${TELEMETRY_PORT}"
echo "命令端口: ${COMMAND_PORT}"
echo

if ping -c "${PING_COUNT}" -W 1 "${AIRBORNE_HOST}" >/dev/null 2>&1; then
  echo "[OK] ping ${AIRBORNE_HOST}"
else
  echo "[FAIL] ping ${AIRBORNE_HOST}"
fi

if check_tcp_port "${AIRBORNE_HOST}" "${TELEMETRY_PORT}"; then
  echo "[OK] telemetry port ${TELEMETRY_PORT}"
else
  echo "[FAIL] telemetry port ${TELEMETRY_PORT}"
fi

if check_tcp_port "${AIRBORNE_HOST}" "${COMMAND_PORT}"; then
  echo "[OK] command port ${COMMAND_PORT}"
else
  echo "[FAIL] command port ${COMMAND_PORT}"
fi
