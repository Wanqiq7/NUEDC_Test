#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT_DIR}/scripts/check_ground_control_network.sh"
TEMP_DIR="$(mktemp -d)"
FAKE_BIN="${TEMP_DIR}/bin"
CALL_LOG="${TEMP_DIR}/calls.log"

cleanup() {
  rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${FAKE_BIN}"

cat > "${FAKE_BIN}/ping" <<'EOF'
#!/usr/bin/env bash
printf 'ping %s\n' "$*" >> "${CALL_LOG}"
[[ "${PING_RESULT:-success}" == "success" ]]
EOF

cat > "${FAKE_BIN}/nc" <<'EOF'
#!/usr/bin/env bash
printf 'nc %s\n' "$*" >> "${CALL_LOG}"
[[ ",${OPEN_PORTS:-5557,5558}," == *",${*: -1},"* ]]
EOF

chmod +x "${FAKE_BIN}/ping" "${FAKE_BIN}/nc"

PATH="${FAKE_BIN}:${PATH}" CALL_LOG="${CALL_LOG}" "${SCRIPT}"
rg -F -x "ping -c 2 -W 1 10.42.0.2" "${CALL_LOG}" >/dev/null

if PATH="${FAKE_BIN}:${PATH}" CALL_LOG="${CALL_LOG}" PING_RESULT=failure OPEN_PORTS=5557 \
    "${SCRIPT}" --host 10.42.0.9 >/dev/null 2>&1; then
  echo "Expected failed checks to return a nonzero status." >&2
  exit 1
fi

echo "test_check_ground_control_network: PASS"
