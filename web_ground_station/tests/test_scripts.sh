#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
mkdir -p "${TMP_DIR}/bin"
printf '#!/usr/bin/env bash\nexit 0\n' > "${TMP_DIR}/bin/network-check"
printf '#!/usr/bin/env bash\nexit 0\n' > "${TMP_DIR}/planner"
chmod +x "${TMP_DIR}/bin/network-check" "${TMP_DIR}/planner"
cp "${ROOT_DIR}/runtime/web_ground_station.env" "${TMP_DIR}/web_ground_station.env"
printf 'NUEDC_PLANNER_CLI=%s\n' "${TMP_DIR}/planner" >> "${TMP_DIR}/web_ground_station.env"
cat > "${TMP_DIR}/bin/uv" <<'EOF'
#!/usr/bin/env bash
set -e
printf '%s\n' "$*" > "${FAKE_LOG}"
printf '%s\n%s\n' "${NUEDC_RUNTIME_DIR}" "${NUEDC_PLANNER_CLI}" > "${FAKE_ENV_LOG}"
[[ "$*" == *"uvicorn"* ]] && exit 0
EOF
chmod +x "${TMP_DIR}/bin/uv"
FAKE_LOG="${TMP_DIR}/uv.log" FAKE_ENV_LOG="${TMP_DIR}/uv.env.log" \
  PATH="${TMP_DIR}/bin:${PATH}" \
  NUEDC_NETWORK_CHECK="${TMP_DIR}/bin/network-check" \
  NUEDC_WEB_ENV_FILE="${TMP_DIR}/web_ground_station.env" \
  bash "${ROOT_DIR}/web_ground_station/scripts/start_competition.sh"
grep -q 'uvicorn' "${TMP_DIR}/uv.log"
grep -q -- '--host 0.0.0.0 --port 8000' "${TMP_DIR}/uv.log"
[[ "$(sed -n '1p' "${TMP_DIR}/uv.env.log")" == "${ROOT_DIR}/runtime" ]]
[[ "$(sed -n '2p' "${TMP_DIR}/uv.env.log")" == "${TMP_DIR}/planner" ]]
! grep -q -- '--reload' "${TMP_DIR}/uv.log"
! grep -q 'pnpm install\|uv sync\|pid' "${TMP_DIR}/uv.log"
echo "web ground station scripts: PASS"
