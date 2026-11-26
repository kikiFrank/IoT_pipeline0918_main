#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIGLIP_PROCESS_KIND=scene
. "$SCRIPT_DIR/worker_process.sh"
exec 9>"$SIGLIP_OUT/worker.control.lock"
flock -w 5 9 || { echo SIGLIP_SCENE_CONTROL_BUSY; exit 4; }
if ! siglip_read_pidfile || ! siglip_pid_still_exists; then
  [[ -z "$(siglip_find_exact)" ]] || { echo SIGLIP_SCENE_UNTRACKED_INSTANCE; exit 5; }
  echo SIGLIP_SCENE_ALREADY_STOPPED
  exit 0
fi
siglip_matches "$SIGLIP_PID" "$SIGLIP_START" "$SIGLIP_FILE_EXE" "$SIGLIP_FILE_CONFIG" ||
  { echo SIGLIP_SCENE_PID_IDENTITY_MISMATCH; exit 5; }
kill -TERM "$SIGLIP_PID"
for (( second=0; second<20; second++ )); do
  if ! siglip_matches "$SIGLIP_PID" "$SIGLIP_START" "$SIGLIP_FILE_EXE" "$SIGLIP_FILE_CONFIG"; then
    rm -f -- "$SIGLIP_PIDFILE"
    echo SIGLIP_SCENE_STOP_OK
    exit 0
  fi
  sleep 1
done
echo SIGLIP_SCENE_STOP_TIMEOUT
exit 9
