#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT_DIR}/scripts/start_ground_hotspot.sh"
TEMP_DIR="$(mktemp -d)"
FAKE_BIN="${TEMP_DIR}/bin"
CALL_LOG="${TEMP_DIR}/calls.log"
RUNTIME_DIR="${TEMP_DIR}/runtime"

cleanup() {
  rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${FAKE_BIN}"

cat > "${FAKE_BIN}/nmcli" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "${CALL_LOG}"
exit 0
EOF

cat > "${FAKE_BIN}/ip" <<'EOF'
#!/usr/bin/env bash
if [[ "$*" == "-4 -o addr show dev wlan0" ]]; then
  echo "3: wlan0    inet 10.42.0.1/24 brd 10.42.0.255 scope global wlan0"
fi
EOF

chmod +x "${FAKE_BIN}/nmcli" "${FAKE_BIN}/ip"

assert_log_contains() {
  local expected="$1"
  if ! rg -F -x -- "${expected}" "${CALL_LOG}" >/dev/null; then
    echo "Expected nmcli call not found: ${expected}" >&2
    cat "${CALL_LOG}" >&2
    exit 1
  fi
}

PATH="${FAKE_BIN}:${PATH}" \
CALL_LOG="${CALL_LOG}" \
NUEDC_WIFI_IFACE=wlan0 \
NUEDC_HOTSPOT_PASSWORD=12345678 \
NUEDC_RUNTIME_DIR="${RUNTIME_DIR}" \
"${SCRIPT}"

assert_log_contains "-t -f NAME connection show"
assert_log_contains "connection add type wifi ifname wlan0 con-name NUEDC-Ground-Hotspot autoconnect yes ssid NUEDC-Ground"
assert_log_contains "connection modify NUEDC-Ground-Hotspot 802-11-wireless.mode ap"
assert_log_contains "connection modify NUEDC-Ground-Hotspot ipv4.method shared ipv4.addresses 10.42.0.1/24"
assert_log_contains "connection modify NUEDC-Ground-Hotspot wifi-sec.key-mgmt wpa-psk"
assert_log_contains "connection modify NUEDC-Ground-Hotspot wifi-sec.psk 12345678"
assert_log_contains "connection up NUEDC-Ground-Hotspot ifname wlan0"

ENV_FILE="${RUNTIME_DIR}/ground_control_network.env"
rg -F -x "export NUEDC_AIRBORNE_HOST=10.42.0.2" "${ENV_FILE}"
rg -F -x "export NUEDC_TELEMETRY_PORT=5557" "${ENV_FILE}"
rg -F -x "export NUEDC_COMMAND_PORT=5558" "${ENV_FILE}"

echo "test_start_ground_hotspot: PASS"
