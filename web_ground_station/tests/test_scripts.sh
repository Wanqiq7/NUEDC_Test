#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
mkdir -p "${TMP_DIR}/bin" "${TMP_DIR}/dist" "${TMP_DIR}/runtime"
printf '<!doctype html>\n' > "${TMP_DIR}/dist/index.html"
printf '#!/usr/bin/env bash\nexit 0\n' > "${TMP_DIR}/planner"
chmod +x "${TMP_DIR}/planner"
cp "${ROOT_DIR}/runtime/web_ground_station.env" "${TMP_DIR}/web_ground_station.env"
printf 'NUEDC_RUNTIME_DIR=%s\nNUEDC_PLANNER_CLI=%s\n' \
  "${TMP_DIR}/runtime" "${TMP_DIR}/planner" >> "${TMP_DIR}/web_ground_station.env"
cat > "${TMP_DIR}/bin/uv" <<'EOF'
#!/usr/bin/env bash
printf 'uv %s\n' "$*" >> "${FORBIDDEN_LOG}"
exit 99
EOF
cat > "${TMP_DIR}/bin/protoc" <<'EOF'
#!/usr/bin/env bash
printf 'protoc %s\n' "$*" >> "${FORBIDDEN_LOG}"
exit 99
EOF
cat > "${TMP_DIR}/bin/uvicorn" <<'EOF'
#!/usr/bin/env bash
set -e
printf '%s\n' "$*" >> "${UVICORN_LOG}"
printf '%s\n%s\n' "${NUEDC_RUNTIME_DIR}" "${NUEDC_PLANNER_CLI}" > "${FAKE_ENV_LOG}"
EOF
chmod +x "${TMP_DIR}/bin/uv" "${TMP_DIR}/bin/protoc" "${TMP_DIR}/bin/uvicorn"

cat > "${TMP_DIR}/bin/network-check" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "${NETWORK_LOG}"
exit "${NETWORK_EXIT:-0}"
EOF
cat > "${TMP_DIR}/bin/preflight-check" <<'EOF'
#!/usr/bin/env bash
printf 'called\n' >> "${PREFLIGHT_LOG}"
exit "${PREFLIGHT_EXIT:-0}"
EOF
chmod +x "${TMP_DIR}/bin/network-check" "${TMP_DIR}/bin/preflight-check"

run_competition() {
  UVICORN_LOG="${TMP_DIR}/uvicorn.log" FAKE_ENV_LOG="${TMP_DIR}/uvicorn.env.log" \
    FORBIDDEN_LOG="${TMP_DIR}/forbidden.log" \
    NETWORK_LOG="${TMP_DIR}/network.log" PREFLIGHT_LOG="${TMP_DIR}/preflight.log" \
    PATH="${TMP_DIR}/bin:${PATH}" \
    NUEDC_NETWORK_CHECK="${TMP_DIR}/bin/network-check" \
    NUEDC_WEB_PREFLIGHT_CHECK="${TMP_DIR}/bin/preflight-check" \
    NUEDC_UVICORN_BIN="${TMP_DIR}/bin/uvicorn" \
    NUEDC_WEB_ENV_FILE="${TMP_DIR}/web_ground_station.env" \
    "$@" bash "${ROOT_DIR}/web_ground_station/scripts/start_competition.sh"
}

run_competition_with_real_preflight() {
  local env_file="$1"
  local frontend_dist_dir="$2"
  UVICORN_LOG="${TMP_DIR}/uvicorn.log" FAKE_ENV_LOG="${TMP_DIR}/uvicorn.env.log" \
    FORBIDDEN_LOG="${TMP_DIR}/forbidden.log" \
    NETWORK_LOG="${TMP_DIR}/network.log" \
    PATH="${TMP_DIR}/bin:${PATH}" \
    NUEDC_NETWORK_CHECK="${TMP_DIR}/bin/network-check" \
    NUEDC_UVICORN_BIN="${TMP_DIR}/bin/uvicorn" \
    NUEDC_WEB_ENV_FILE="${env_file}" \
    NUEDC_FRONTEND_DIST_DIR="${frontend_dist_dir}" \
    bash "${ROOT_DIR}/web_ground_station/scripts/start_competition.sh"
}

rm -f "${TMP_DIR}/uvicorn.log" "${TMP_DIR}/forbidden.log" \
  "${TMP_DIR}/network.log" "${TMP_DIR}/preflight.log"
run_competition env
[[ "$(cat "${TMP_DIR}/network.log")" == \
  "--host 10.42.0.2 --telemetry-port 5557 --command-port 5558" ]]
[[ "$(cat "${TMP_DIR}/preflight.log")" == "called" ]]
grep -q 'nuedc_web_gateway.app:create_app --factory' "${TMP_DIR}/uvicorn.log"
grep -q -- '--host 0.0.0.0 --port 8000' "${TMP_DIR}/uvicorn.log"
[[ "$(sed -n '1p' "${TMP_DIR}/uvicorn.env.log")" == "${TMP_DIR}/runtime" ]]
[[ "$(sed -n '2p' "${TMP_DIR}/uvicorn.env.log")" == "${TMP_DIR}/planner" ]]
! grep -q -- '--reload' "${TMP_DIR}/uvicorn.log"
[[ ! -e "${TMP_DIR}/forbidden.log" ]]

rm -f "${TMP_DIR}/uvicorn.log" "${TMP_DIR}/forbidden.log" \
  "${TMP_DIR}/network.log" "${TMP_DIR}/preflight.log"
if run_competition env NETWORK_EXIT=1; then
  echo "competition startup unexpectedly ignored a network-check failure" >&2
  exit 1
fi
[[ ! -e "${TMP_DIR}/preflight.log" ]]
[[ ! -e "${TMP_DIR}/uvicorn.log" ]]
[[ ! -e "${TMP_DIR}/forbidden.log" ]]

rm -f "${TMP_DIR}/uvicorn.log" "${TMP_DIR}/forbidden.log" \
  "${TMP_DIR}/network.log" "${TMP_DIR}/preflight.log"
if run_competition env PREFLIGHT_EXIT=1; then
  echo "competition startup unexpectedly ignored a preflight failure" >&2
  exit 1
fi
[[ "$(cat "${TMP_DIR}/network.log")" == \
  "--host 10.42.0.2 --telemetry-port 5557 --command-port 5558" ]]
[[ "$(cat "${TMP_DIR}/preflight.log")" == "called" ]]
[[ ! -e "${TMP_DIR}/uvicorn.log" ]]
[[ ! -e "${TMP_DIR}/forbidden.log" ]]

NUEDC_WEB_ENV_FILE="${TMP_DIR}/web_ground_station.env" \
  NUEDC_FRONTEND_DIST_DIR="${TMP_DIR}/dist" \
  NUEDC_UVICORN_BIN="${TMP_DIR}/bin/uvicorn" \
  bash "${ROOT_DIR}/web_ground_station/scripts/check_web_ground_station.sh" >/dev/null

cp "${TMP_DIR}/web_ground_station.env" "${TMP_DIR}/missing-planner.env"
printf 'NUEDC_PLANNER_CLI=%s\n' "${TMP_DIR}/missing-planner" >> "${TMP_DIR}/missing-planner.env"
rm -f "${TMP_DIR}/uvicorn.log" "${TMP_DIR}/forbidden.log" "${TMP_DIR}/network.log"
if run_competition_with_real_preflight \
  "${TMP_DIR}/missing-planner.env" "${TMP_DIR}/dist" \
  >"${TMP_DIR}/missing-planner.out" 2>&1; then
  echo "competition startup unexpectedly accepted a missing planner" >&2
  exit 1
fi
grep -q '规划器不可执行' "${TMP_DIR}/missing-planner.out"
[[ ! -e "${TMP_DIR}/uvicorn.log" ]]
[[ ! -e "${TMP_DIR}/forbidden.log" ]]
[[ "$(cat "${TMP_DIR}/network.log")" == \
  "--host 10.42.0.2 --telemetry-port 5557 --command-port 5558" ]]

rm -f "${TMP_DIR}/uvicorn.log" "${TMP_DIR}/forbidden.log" "${TMP_DIR}/network.log"
if run_competition_with_real_preflight \
  "${TMP_DIR}/web_ground_station.env" "${TMP_DIR}/missing-dist" \
  >"${TMP_DIR}/missing-dist.out" 2>&1; then
  echo "competition startup unexpectedly accepted a missing frontend dist" >&2
  exit 1
fi
grep -q '缺少 frontend/dist' "${TMP_DIR}/missing-dist.out"
[[ ! -e "${TMP_DIR}/uvicorn.log" ]]
[[ ! -e "${TMP_DIR}/forbidden.log" ]]
[[ "$(cat "${TMP_DIR}/network.log")" == \
  "--host 10.42.0.2 --telemetry-port 5557 --command-port 5558" ]]
echo "web ground station scripts: PASS"
