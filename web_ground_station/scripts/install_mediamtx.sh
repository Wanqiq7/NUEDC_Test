#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="1.19.2"
ARCHIVE="mediamtx_v${VERSION}_linux_amd64.tar.gz"
EXPECTED_SHA256="f9c601cc303ceca8fad2883917b022882672c5bc56311e92dbceb16e5f20c60c"
URL="https://github.com/bluenviron/mediamtx/releases/download/v${VERSION}/${ARCHIVE}"
DESTINATION="${ROOT_DIR}/web_ground_station/vendor/mediamtx"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

curl --fail --location --silent --show-error "${URL}" --output "${TMP_DIR}/${ARCHIVE}"
printf '%s  %s\n' "${EXPECTED_SHA256}" "${TMP_DIR}/${ARCHIVE}" | sha256sum --check --status
tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "${TMP_DIR}" mediamtx
mkdir -p "${DESTINATION}"
install -m 0755 "${TMP_DIR}/mediamtx" "${DESTINATION}/mediamtx"
printf 'MediaMTX v%s installed at %s\n' "${VERSION}" "${DESTINATION}/mediamtx"
