#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: verify_cross_repo_manifest_contract.sh \
  --ground-repo PATH --airborne-repo PATH --model PATH [--output PATH]

Create one manifest with the Ground producer, then verify the same file with
both the Ground verifier and the Airborne verifier.
EOF
}

GROUND_REPO=""
AIRBORNE_REPO=""
MODEL_PATH=""
OUTPUT_PATH=""
while (($#)); do
  case "$1" in
    --ground-repo) GROUND_REPO="$2"; shift 2 ;;
    --airborne-repo) AIRBORNE_REPO="$2"; shift 2 ;;
    --model) MODEL_PATH="$2"; shift 2 ;;
    --output) OUTPUT_PATH="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "${GROUND_REPO}" && -n "${AIRBORNE_REPO}" && -n "${MODEL_PATH}" ]] || {
  usage >&2
  exit 2
}
GROUND_REPO="$(cd "${GROUND_REPO}" && pwd)"
AIRBORNE_REPO="$(cd "${AIRBORNE_REPO}" && pwd)"
MODEL_PATH="$(realpath "${MODEL_PATH}")"
PRODUCER="${GROUND_REPO}/web_ground_station/scripts/deployment_manifest.py"
CONSUMER="${AIRBORNE_REPO}/src/nuedc_airborne/airborne_bringup/scripts/verify_deployment_manifest.py"
[[ -f "${PRODUCER}" ]] || { echo "missing Ground manifest producer: ${PRODUCER}" >&2; exit 2; }
[[ -f "${CONSUMER}" ]] || { echo "missing Airborne manifest verifier: ${CONSUMER}" >&2; exit 2; }

TEMP_DIR=""
if [[ -z "${OUTPUT_PATH}" ]]; then
  TEMP_DIR="$(mktemp -d)"
  trap 'rm -rf "${TEMP_DIR}"' EXIT
  OUTPUT_PATH="${TEMP_DIR}/deployment_manifest.json"
else
  OUTPUT_PATH="$(realpath -m "${OUTPUT_PATH}")"
fi

python3 "${PRODUCER}" create \
  --ground-repo "${GROUND_REPO}" \
  --airborne-repo "${AIRBORNE_REPO}" \
  --model "${MODEL_PATH}" \
  --output "${OUTPUT_PATH}"
python3 "${PRODUCER}" verify \
  --manifest "${OUTPUT_PATH}" --repo "${GROUND_REPO}" --role ground \
  --proto "${GROUND_REPO}/shared/proto/messages.proto"
python3 "${CONSUMER}" "${OUTPUT_PATH}" "${AIRBORNE_REPO}" \
  "${AIRBORNE_REPO}/shared/proto/messages.proto" "${MODEL_PATH}"
printf 'cross-repo deployment manifest contract: PASS (%s)\n' "${OUTPUT_PATH}"
