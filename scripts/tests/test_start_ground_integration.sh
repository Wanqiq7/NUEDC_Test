#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT_DIR}/scripts/start_ground_integration.sh"
TEMP_DIR="$(mktemp -d)"
CALL_LOG="${TEMP_DIR}/calls.log"
ENV_FILE="${TEMP_DIR}/ground_control_network.env"

cleanup() {
  rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

cat > "${TEMP_DIR}/network_check" <<'EOF'
#!/usr/bin/env bash
printf 'check host=%s telemetry=%s command=%s args=%s\n' \
  "${NUEDC_AIRBORNE_HOST:-}" "${NUEDC_TELEMETRY_PORT:-}" \
  "${NUEDC_COMMAND_PORT:-}" "$*" >> "${CALL_LOG}"
[[ "${CHECK_RESULT:-success}" == "success" ]]
EOF

cat > "${TEMP_DIR}/ground_station_app" <<'EOF'
#!/usr/bin/env bash
printf 'app host=%s telemetry=%s command=%s args=%s\n' \
  "${NUEDC_AIRBORNE_HOST:-}" "${NUEDC_TELEMETRY_PORT:-}" \
  "${NUEDC_COMMAND_PORT:-}" "$*" >> "${CALL_LOG}"
EOF

cat > "${ENV_FILE}" <<'EOF'
export NUEDC_AIRBORNE_HOST=10.42.0.2
export NUEDC_TELEMETRY_PORT=5557
export NUEDC_COMMAND_PORT=5558
EOF

chmod +x "${TEMP_DIR}/network_check" "${TEMP_DIR}/ground_station_app"

CALL_LOG="${CALL_LOG}" \
NUEDC_NETWORK_ENV_FILE="${ENV_FILE}" \
NUEDC_NETWORK_CHECK_PATH="${TEMP_DIR}/network_check" \
NUEDC_GROUND_APP_PATH="${TEMP_DIR}/ground_station_app" \
"${SCRIPT}" -- --test-mode

rg -F -x "check host=10.42.0.2 telemetry=5557 command=5558 args=--host 10.42.0.2 --telemetry-port 5557 --command-port 5558" "${CALL_LOG}" >/dev/null
rg -F -x "app host=10.42.0.2 telemetry=5557 command=5558 args=--test-mode" "${CALL_LOG}" >/dev/null

: > "${CALL_LOG}"
if CALL_LOG="${CALL_LOG}" CHECK_RESULT=failure \
    NUEDC_NETWORK_ENV_FILE="${ENV_FILE}" \
    NUEDC_NETWORK_CHECK_PATH="${TEMP_DIR}/network_check" \
    NUEDC_GROUND_APP_PATH="${TEMP_DIR}/ground_station_app" \
    "${SCRIPT}" >/dev/null 2>&1; then
  echo "Expected a failed preflight to stop the ground-station launch." >&2
  exit 1
fi
if rg -q '^app ' "${CALL_LOG}"; then
  echo "Ground-station app started after a failed preflight." >&2
  exit 1
fi

echo "test_start_ground_integration: PASS"
