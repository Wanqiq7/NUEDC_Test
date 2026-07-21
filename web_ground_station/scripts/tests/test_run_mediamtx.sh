#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SCRIPT="${ROOT_DIR}/web_ground_station/scripts/run_mediamtx.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

cat > "${TMP_DIR}/mediamtx" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "${FAKE_MEDIAMTX_ARGS}"
cp "$1" "${FAKE_MEDIAMTX_CONFIG_COPY}"
trap 'exit 0' TERM INT
while :; do sleep 1; done
EOF
cat > "${TMP_DIR}/ready-check" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "${TMP_DIR}/mediamtx" "${TMP_DIR}/ready-check"
printf 'paths:\n  camera_raw:\n    source: __NUEDC_RTSP_SOURCE__\n' > "${TMP_DIR}/mediamtx.yml"

if NUEDC_MEDIAMTX_BIN="${TMP_DIR}/mediamtx" \
  NUEDC_MEDIAMTX_CONFIG="${TMP_DIR}/mediamtx.yml" \
  bash "${SCRIPT}" >"${TMP_DIR}/missing.out" 2>&1; then
  echo "MediaMTX supervisor accepted missing credentials" >&2
  exit 1
fi
grep -q 'NUEDC_VIDEO_RTSP_USER' "${TMP_DIR}/missing.out"

READY_FILE="${TMP_DIR}/ready"
FAKE_MEDIAMTX_ARGS="${TMP_DIR}/args" \
FAKE_MEDIAMTX_CONFIG_COPY="${TMP_DIR}/runtime-config" \
NUEDC_MEDIAMTX_BIN="${TMP_DIR}/mediamtx" \
NUEDC_MEDIAMTX_CONFIG="${TMP_DIR}/mediamtx.yml" \
NUEDC_MEDIAMTX_READY_CHECK="${TMP_DIR}/ready-check" \
NUEDC_MEDIAMTX_READY_FILE="${READY_FILE}" \
NUEDC_AIRBORNE_HOST="10.42.0.2" \
NUEDC_VIDEO_RTSP_USER="viewer" \
NUEDC_VIDEO_RTSP_PASSWORD="deployment-secret" \
  bash "${SCRIPT}" >"${TMP_DIR}/supervisor.out" 2>&1 &
SUPERVISOR_PID=$!

for _ in $(seq 1 50); do
  [[ -f "${READY_FILE}" ]] && break
  sleep 0.02
done
[[ -f "${READY_FILE}" ]]
[[ "$(cat "${TMP_DIR}/args")" == /tmp/nuedc-mediamtx.*.yml ]]
grep -q 'source: rtsp://viewer:deployment-secret@10.42.0.2:8554/camera_raw' \
  "${TMP_DIR}/runtime-config"
! grep -q 'deployment-secret' "${TMP_DIR}/supervisor.out"

kill "${SUPERVISOR_PID}"
wait "${SUPERVISOR_PID}" || true
if kill -0 "${SUPERVISOR_PID}" 2>/dev/null; then
  echo "MediaMTX supervisor did not stop" >&2
  exit 1
fi

echo "MediaMTX supervisor: PASS"
