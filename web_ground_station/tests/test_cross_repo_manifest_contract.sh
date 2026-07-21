#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT_DIR}/web_ground_station/scripts/verify_cross_repo_manifest_contract.sh"
AIRBORNE_REPO="${1:?usage: test_cross_repo_manifest_contract.sh AIRBORNE_REPO}"
AIRBORNE_REPO="$(cd "${AIRBORNE_REPO}" && pwd)"
AIRBORNE_VERIFIER="${AIRBORNE_REPO}/src/nuedc_airborne/airborne_bringup/scripts/verify_deployment_manifest.py"
[[ -f "${AIRBORNE_VERIFIER}" ]] || {
  echo "missing Airborne verifier: ${AIRBORNE_VERIFIER}" >&2
  exit 2
}

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
GROUND_FIXTURE="${TMP_DIR}/ground"
AIRBORNE_FIXTURE="${TMP_DIR}/airborne"
mkdir -p \
  "${GROUND_FIXTURE}/shared/proto" \
  "${GROUND_FIXTURE}/web_ground_station/scripts" \
  "${AIRBORNE_FIXTURE}/shared/proto" \
  "${AIRBORNE_FIXTURE}/src/nuedc_airborne/airborne_bringup/scripts"
cp "${ROOT_DIR}/shared/proto/messages.proto" \
  "${GROUND_FIXTURE}/shared/proto/messages.proto"
cp "${ROOT_DIR}/shared/proto/messages.proto" \
  "${AIRBORNE_FIXTURE}/shared/proto/messages.proto"
cp "${ROOT_DIR}/web_ground_station/scripts/deployment_manifest.py" \
  "${GROUND_FIXTURE}/web_ground_station/scripts/deployment_manifest.py"
cp "${AIRBORNE_VERIFIER}" \
  "${AIRBORNE_FIXTURE}/src/nuedc_airborne/airborne_bringup/scripts/verify_deployment_manifest.py"
printf 'runtime/deployment_manifest.json\n' > "${GROUND_FIXTURE}/.gitignore"

for repo in "${GROUND_FIXTURE}" "${AIRBORNE_FIXTURE}"; do
  git -C "${repo}" init -q
  git -C "${repo}" config user.name "Contract Test"
  git -C "${repo}" config user.email "contract@example.invalid"
  git -C "${repo}" add .
  git -C "${repo}" commit -q -m fixture
done

MODEL_PATH="${TMP_DIR}/model.rknn"
OUTPUT_PATH="${GROUND_FIXTURE}/runtime/deployment_manifest.json"
printf 'fixture-model' > "${MODEL_PATH}"

bash "${SCRIPT}" \
  --ground-repo "${GROUND_FIXTURE}" \
  --airborne-repo "${AIRBORNE_FIXTURE}" \
  --model "${MODEL_PATH}" \
  --output "${OUTPUT_PATH}"

test -s "${OUTPUT_PATH}"
python3 "${GROUND_FIXTURE}/web_ground_station/scripts/deployment_manifest.py" verify \
  --manifest "${OUTPUT_PATH}" --repo "${GROUND_FIXTURE}" --role ground \
  --proto "${GROUND_FIXTURE}/shared/proto/messages.proto"
python3 "${AIRBORNE_FIXTURE}/src/nuedc_airborne/airborne_bringup/scripts/verify_deployment_manifest.py" \
  "${OUTPUT_PATH}" "${AIRBORNE_FIXTURE}" \
  "${AIRBORNE_FIXTURE}/shared/proto/messages.proto" "${MODEL_PATH}"
