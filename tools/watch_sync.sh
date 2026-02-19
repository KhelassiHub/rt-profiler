#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <user@pi-host> <remote-dir> [local-dir]" >&2
  echo "Example: $0 pi@192.168.1.50 /home/pi/rt-profiler ." >&2
  exit 1
fi

TARGET="$1"
REMOTE_DIR="$2"
LOCAL_DIR="${3:-.}"

RSYNC_EXCLUDES=(
  "--exclude" ".git/"
  "--exclude" "__pycache__/"
  "--exclude" "*.o"
  "--exclude" "*.so"
  "--exclude" "a.out"
  "--exclude" "measurements.log"
)

echo "[watch_sync] Initial sync to ${TARGET}:${REMOTE_DIR}" >&2
ssh "$TARGET" "mkdir -p ${REMOTE_DIR}"
rsync -az --delete "${RSYNC_EXCLUDES[@]}" "${LOCAL_DIR}/" "${TARGET}:${REMOTE_DIR}/"

echo "[watch_sync] Watching ${LOCAL_DIR} for changes..." >&2

if command -v inotifywait >/dev/null 2>&1; then
  inotifywait -m -r -e close_write,create,delete,move \
    --exclude '(^|/)(\.git|__pycache__)(/|$)|\.(o|so)$|measurements\.log$' \
    "${LOCAL_DIR}" | while read -r _; do
      echo "[watch_sync] Change detected, syncing..." >&2
      ssh "$TARGET" "mkdir -p ${REMOTE_DIR}"
      rsync -az --delete "${RSYNC_EXCLUDES[@]}" "${LOCAL_DIR}/" "${TARGET}:${REMOTE_DIR}/"
    done
else
  echo "[watch_sync] inotifywait not found. Falling back to 2s polling." >&2
  last_sum=""
  last_sync_ts=0
  while true; do
    new_sum="$(find "${LOCAL_DIR}" -type f \
      ! -path '*/.git/*' \
      ! -path '*/__pycache__/*' \
      ! -name '*.o' ! -name '*.so' \
      ! -name 'a.out' ! -name 'measurements.log' \
      -printf '%p %T@\n' | sort | sha256sum | awk '{print $1}')"
    if [[ "$new_sum" != "$last_sum" ]]; then
      now_ts="$(date +%s)"
      if (( now_ts - last_sync_ts >= 1 )); then
        last_sum="$new_sum"
        last_sync_ts="$now_ts"
        echo "[watch_sync] Change detected, syncing..." >&2
        ssh "$TARGET" "mkdir -p ${REMOTE_DIR}"
        rsync -az --delete "${RSYNC_EXCLUDES[@]}" "${LOCAL_DIR}/" "${TARGET}:${REMOTE_DIR}/"
      fi
    fi
    sleep 2
  done
fi
