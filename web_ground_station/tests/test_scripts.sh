#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
mkdir -p "${TMP_DIR}/bin" "${ROOT_DIR}/build/shared/cpp"
printf '#!/usr/bin/env bash\nexit 0\n' > "${TMP_DIR}/bin/network-check"
printf '#!/usr/bin/env bash\nexit 0\n' > "${ROOT_DIR}/build/shared/cpp/h_route_planner_cli"
chmod +x "${TMP_DIR}/bin/network-check" "${ROOT_DIR}/build/shared/cpp/h_route_planner_cli"
cat > "${TMP_DIR}/bin/uv" <<'EOF'
#!/usr/bin/env bash
set -e
printf '%s\n' "$*" > "${FAKE_LOG}"
[[ "$*" == *"uvicorn"* ]] && exit 0
EOF
chmod +x "${TMP_DIR}/bin/uv"
FAKE_LOG="${TMP_DIR}/uv.log" PATH="${TMP_DIR}/bin:${PATH}" \
  NUEDC_NETWORK_CHECK="${TMP_DIR}/bin/network-check" \
  bash "${ROOT_DIR}/web_ground_station/scripts/start_competition.sh"
grep -q 'uvicorn' "${TMP_DIR}/uv.log"
! grep -q -- '--reload' "${TMP_DIR}/uv.log"
! grep -q 'pnpm install\|uv sync\|pid' "${TMP_DIR}/uv.log"
echo "web ground station scripts: PASS"
