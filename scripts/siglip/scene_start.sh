#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIGLIP_PROCESS_KIND=scene
. "$SCRIPT_DIR/worker_process.sh"
exec 9>"$SIGLIP_OUT/worker.control.lock"
flock -w 5 9 || { echo SIGLIP_SCENE_CONTROL_BUSY; exit 4; }
if siglip_read_pidfile && siglip_matches "$SIGLIP_PID" "$SIGLIP_START" "$SIGLIP_FILE_EXE" "$SIGLIP_FILE_CONFIG"; then
  [[ "$SIGLIP_FILE_CONFIG" == "$SIGLIP_CONFIG" ]] || exit 5
  echo "SIGLIP_SCENE_ALREADY_RUNNING pid=$SIGLIP_PID"
  exit 0
fi
if siglip_read_pidfile && siglip_pid_still_exists; then
  echo SIGLIP_SCENE_PID_IDENTITY_MISMATCH
  exit 5
fi
[[ -z "$(siglip_find_exact)" ]] || { echo SIGLIP_SCENE_UNTRACKED_INSTANCE; exit 5; }
[[ -x "$SIGLIP_EXE" && -f "$SIGLIP_CONFIG" ]] || { echo SIGLIP_SCENE_MISSING_FILE; exit 6; }
export AIBOX_ROOT="$SIGLIP_ROOT"
. "$SIGLIP_ROOT/scripts/runtime_graphics_env.sh"
export GST_PLUGIN_PATH="$SIGLIP_ROOT/runtime_plugins"
export GST_ML_MODULES_DIR="$SIGLIP_ROOT/plugins/ml_modules"
export GST_REGISTRY="$SIGLIP_OUT/gst_registry.bin"
log="$SIGLIP_OUT/scene-$(date +%Y%m%d-%H%M%S)-$$.log"
nohup stdbuf -oL -eL "$SIGLIP_EXE" "$SIGLIP_CONFIG" >"$log" 2>&1 9>&- </dev/null &
pid=$!
sleep 1
start="$(siglip_starttime "$pid")" || { tail -n 40 "$log"; exit 7; }
printf '%s\t%s\t%s\t%s\t%s\n' "$pid" "$start" "$SIGLIP_EXE" "$SIGLIP_CONFIG" "$log" > "$SIGLIP_PIDFILE"
ln -sfn "$(basename "$log")" "$SIGLIP_OUT/scene.log"
for (( second=0; second<110; second++ )); do
  if ! siglip_matches "$pid" "$start" "$SIGLIP_EXE" "$SIGLIP_CONFIG"; then
    tail -n 40 "$log"
    exit 8
  fi
  if grep -Fq '[SIGLIP_SCENE_SCORE]' "$log"; then
    echo "SIGLIP_SCENE_START_OK pid=$pid log=$log"
    exit 0
  fi
  sleep 1
done
if siglip_matches "$pid" "$start" "$SIGLIP_EXE" "$SIGLIP_CONFIG"; then kill -TERM "$pid"; fi
echo "SIGLIP_SCENE_START_TIMEOUT log=$log"
exit 9
