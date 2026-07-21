#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MEDIAMTX_BIN="${NUEDC_MEDIAMTX_BIN:-${ROOT_DIR}/web_ground_station/vendor/mediamtx/mediamtx}"
MEDIAMTX_CONFIG="${NUEDC_MEDIAMTX_CONFIG:-${ROOT_DIR}/web_ground_station/config/mediamtx.yml}"
READY_FILE="${NUEDC_MEDIAMTX_READY_FILE:-${ROOT_DIR}/runtime/mediamtx.ready}"
READY_CHECK="${NUEDC_MEDIAMTX_READY_CHECK:-}"
AIRBORNE_HOST="${NUEDC_AIRBORNE_HOST:-10.42.0.2}"
[[ "${MEDIAMTX_BIN}" = /* ]] || MEDIAMTX_BIN="${ROOT_DIR}/${MEDIAMTX_BIN}"
[[ "${MEDIAMTX_CONFIG}" = /* ]] || MEDIAMTX_CONFIG="${ROOT_DIR}/${MEDIAMTX_CONFIG}"

: "${NUEDC_VIDEO_RTSP_USER:?NUEDC_VIDEO_RTSP_USER is required}"
: "${NUEDC_VIDEO_RTSP_PASSWORD:?NUEDC_VIDEO_RTSP_PASSWORD is required}"
[[ "${NUEDC_VIDEO_RTSP_USER}" =~ ^[A-Za-z0-9._~-]+$ ]] || {
  echo "NUEDC_VIDEO_RTSP_USER contains unsupported URL characters" >&2; exit 1;
}
[[ "${NUEDC_VIDEO_RTSP_PASSWORD}" =~ ^[A-Za-z0-9._~-]+$ ]] || {
  echo "NUEDC_VIDEO_RTSP_PASSWORD contains unsupported URL characters" >&2; exit 1;
}
[[ -x "${MEDIAMTX_BIN}" ]] || { echo "MediaMTX 不可执行: ${MEDIAMTX_BIN}" >&2; exit 1; }
[[ -f "${MEDIAMTX_CONFIG}" ]] || { echo "缺少 MediaMTX 配置: ${MEDIAMTX_CONFIG}" >&2; exit 1; }

RTSP_SOURCE="rtsp://${NUEDC_VIDEO_RTSP_USER}:${NUEDC_VIDEO_RTSP_PASSWORD}@${AIRBORNE_HOST}:8554/camera_raw"
RUNTIME_CONFIG="$(mktemp /tmp/nuedc-mediamtx.XXXXXX.yml)"
chmod 600 "${RUNTIME_CONFIG}"
sed "s|__NUEDC_RTSP_SOURCE__|${RTSP_SOURCE}|" \
  "${MEDIAMTX_CONFIG}" > "${RUNTIME_CONFIG}"

child_pid=""
stopping=0
cleanup() {
  stopping=1
  rm -f "${READY_FILE}"
  rm -f "${RUNTIME_CONFIG}"
  if [[ -n "${child_pid}" ]] && kill -0 "${child_pid}" 2>/dev/null; then
    kill "${child_pid}" 2>/dev/null || true
    wait "${child_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT TERM INT

is_ready() {
  if [[ -n "${READY_CHECK}" ]]; then
    "${READY_CHECK}"
  else
    curl --fail --silent --show-error --max-time 1 http://127.0.0.1:9997/v3/paths/list >/dev/null
  fi
}

backoff=1
while [[ ${stopping} -eq 0 ]]; do
  rm -f "${READY_FILE}"
  "${MEDIAMTX_BIN}" "${RUNTIME_CONFIG}" &
  child_pid=$!

  ready=0
  for _ in $(seq 1 50); do
    if ! kill -0 "${child_pid}" 2>/dev/null; then
      break
    fi
    if is_ready; then
      mkdir -p "$(dirname "${READY_FILE}")"
      touch "${READY_FILE}"
      ready=1
      break
    fi
    sleep 0.1
  done

  if [[ ${ready} -eq 0 ]] && kill -0 "${child_pid}" 2>/dev/null; then
    echo "MediaMTX 就绪检查超时" >&2
    kill "${child_pid}" 2>/dev/null || true
  fi
  wait "${child_pid}" 2>/dev/null || true
  child_pid=""
  rm -f "${READY_FILE}"
  [[ ${stopping} -eq 0 ]] || break
  sleep "${backoff}"
  (( backoff < 2 )) && backoff=$((backoff * 2))
done
