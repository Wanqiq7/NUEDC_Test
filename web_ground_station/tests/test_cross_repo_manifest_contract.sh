#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT_DIR}/web_ground_station/scripts/verify_cross_repo_manifest_contract.sh"

bash "${SCRIPT}" --help >/dev/null
