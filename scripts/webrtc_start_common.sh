#!/bin/bash
# Shared pipeline start body. Caller must set:
#   R SNAP OUT AGG_NEED LOG
# and AIBOX_ROOT. No SIGKILL of a live pipeline.
# Output is the in-process annotated RTSP server on :8555; detection events
# go to NATS (detection.frame). There is no static HTML page or :8080 server.
set +e
[ -n "${R:-}" ] || { echo COMMON_MISSING_R; exit 3; }
cd "$R" || { echo ROOT_MISSING="$R"; exit 3; }

DEMO="$R/build_unified/aibox_stream_pipeline_demo"
mkdir -p "$OUT" || exit 4
PIDFILE="$OUT/demo.pid"

demo_starttime() {
  local stat
  local -a fields
  stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
  read -r -a fields <<< "${stat##*) }"
  [[ ${#fields[@]} -ge 20 && "${fields[0]}" != Z ]] || return 1
  printf '%s\n' "${fields[19]}"
}

demo_owned() {
  [[ "$demo_pid" =~ ^[1-9][0-9]*$ && -n "$demo_start" ]] || return 1
  [[ "$(demo_starttime "$demo_pid")" == "$demo_start" ]] || return 1
  [[ "$(readlink -f "/proc/$demo_pid/exe" 2>/dev/null)" == "$(readlink -f "$DEMO")" ]] || return 1
  local argument previous=''
  while IFS= read -r -d '' argument; do
    [[ "$previous" == --snapshot && "$argument" == "$SNAP" ]] && return 0
    previous="$argument"
  done 2>/dev/null < "/proc/$demo_pid/cmdline"
  return 1
}

demo_stop() {
  local demo_pid demo_start
  if [[ ! -f "$PIDFILE" ]] || ! read -r demo_pid demo_start < "$PIDFILE" || ! demo_owned; then
    echo STOPPED_ALREADY
    return 0
  fi
  kill -TERM "$demo_pid" 2>/dev/null
  local elapsed=0
  while demo_owned; do
    if (( elapsed >= ${TERM_GRACE:-30} )); then
      echo "STOP_STUCK pid=$demo_pid"
      return 1
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  rm -f -- "$PIDFILE" || return 1
  echo STOPPED_CLEAN
}

exec 9>"$R/output/independent-cnn.lock" || exit 4
flock -n 9 || { echo INDEPENDENT_CNN_OPERATION_IN_PROGRESS; exit 4; }
if [[ "${1:-}" == stop ]]; then
  demo_stop
  exit $?
fi

if [ -d "$R/runtime_plugins" ]; then
  PLUGINS="$R/runtime_plugins"
else
  PLUGINS="$R/plugins"
fi

# A board may carry a project-scoped QTI runtime that matches this build.
# Keep the system runtime as the fallback and leave boards without this
# directory unchanged. (165 compat stack: 2.43 vendor plugins/libs.)
if [ -d "$R/runtime_vendor_plugins" ]; then
  PLUGINS="$R/runtime_vendor_plugins:$PLUGINS"
fi
if [ -d "$R/runtime_vendor_libs" ]; then
  export LD_LIBRARY_PATH="$R/runtime_vendor_libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

[ -x "$DEMO" ] || { echo MISSING_DEMO="$DEMO"; exit 11; }
[ -f "$SNAP" ] || { echo MISSING_SNAP="$SNAP"; exit 11; }
echo "SHA=$(sha256sum "$DEMO" | awk '{print $1}')"
echo "PLUGINS=$PLUGINS"
echo "SNAP=$SNAP"

ulimit -n 65536 2>/dev/null || true
# Dedicated display environment is set by runtime_graphics_env.sh.
# E2 fix: shared graphics env (Adreno ICD pin + profile-versioned registry).
# shellcheck disable=SC1091
. "$R/scripts/runtime_graphics_env.sh"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export LIBSEAT_BACKEND="${LIBSEAT_BACKEND:-seatd}"
export GST_PLUGIN_PATH="$PLUGINS"
# Keep board/project-specific plugin discovery isolated from the user's
# global GStreamer registry. runtime_graphics_env.sh already exports a
# profile-versioned registry; this default only applies when it was not
# sourced (fallback).
export GST_REGISTRY="${GST_REGISTRY:-$OUT/gst-registry.bin}"
export GST_REGISTRY_UPDATE=1
# Caller may set GST_DEBUG/GST_DEBUG_FILE for targeted diagnostics
# (e.g. flowtap rounds use GST_DEBUG="v4l2videodec:5"); default stays 0.
export GST_DEBUG="${GST_DEBUG:-0}"
# Board-local (frozen 743 tree): this board's weston emits benign startup
# atomic/repaint EINVAL noise; downgrade to warning by default so the start
# scripts run with zero manual env exports. Still overridable.
export AIBOX_GFX_EINVAL_WARN="${AIBOX_GFX_EINVAL_WARN:-1}"
export AIBOX_VPU_RESET_POLICY="${AIBOX_VPU_RESET_POLICY:-if-needed}"
export AIBOX_WESTON_RESET_POLICY="${AIBOX_WESTON_RESET_POLICY:-never}"
export AIBOX_WEBRTC_SPLITTER=demux
# NOTE: AIBOX_CAMSPLIT_MAX_ENC is ignored in annotated RTSP mode; all cameras
# get a persistent static encoder chain before PLAYING.
# shellcheck disable=SC1091
. "$R/scripts/vpu_health.sh"

if pgrep -f aibox_stream_pipeline_demo >/dev/null; then
  echo "PIPELINE_ALREADY_RUNNING: stop it with its own stop script first"
  exit 4
fi

ss -ltn | grep -q ':4222 ' || { echo NATS_4222_FAIL; exit 5; }
echo NATS_OK

# Expert P0 gate (2026-09-06): graphics runtime fail-fast, see
# scripts/aibox_graphics_gate.sh (shared with tmp A/B runs).
# shellcheck disable=SC1091
. "$R/scripts/aibox_graphics_gate.sh"
bash "$R/scripts/_start_adreno_weston_forceon.sh" || exit 7
aibox_graphics_gate_for_start || exit 7

aibox_vpu_gate_for_start "$R" || exit 8

if [ -n "${AIBOX_RUN_SECONDS:-}" ]; then
  run_args=(--run-seconds "$AIBOX_RUN_SECONDS")
else
  run_args=(--run-forever)
fi
echo "RUN_ARG=${run_args[*]}"

rm -f -- "$PIDFILE" "$LOG" || exit 8
nohup stdbuf -oL -eL "$DEMO" --snapshot "$SNAP" "${run_args[@]}" >"$LOG" 2>&1 9>&- &
demo_pid=$!
demo_start=$(demo_starttime "$demo_pid") || { echo BOOT_DEAD; exit 8; }
printf '%s %s\n' "$demo_pid" "$demo_start" > "$PIDFILE" || { kill -TERM "$demo_pid"; exit 8; }
trap 'demo_stop' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "STARTED_PID=$demo_pid"

# P0-2: BOOT_OK is ONLY the verified [PIPELINE_READY] state=PLAYING line —
# "Setting pipeline to PLAYING" is an intent, not a state confirmation.
i=0
while [ "$i" -lt 90 ]; do
  if grep -aq '\[PIPELINE_READY\] state=PLAYING' "$LOG" 2>/dev/null; then echo BOOT_OK; break; fi
  if [[ "$(demo_starttime "$demo_pid")" != "$demo_start" ]]; then echo BOOT_DEAD; tail -n 40 "$LOG"; exit 8; fi
  # Expert P0: never accept PLAYING on a broken graphics runtime — any
  # Wayland/EGL/GBM init failure in the demo log aborts immediately.
  # Only genuine EGL INIT failures abort the boot here. The overlay's
  # per-run MapGbm/bo-cpu-address startup noise appears in EVERY run
  # (healthy or not) and must NOT kill the start (it stays FATAL in the
  # weston-log gate instead — expert §9).
  if grep -aq 'Failed to get Wayland EGL display\|Failed to initialize Wayland EGL display\|Failed to initialize X11 EGL display' "$LOG" 2>/dev/null; then
    echo BOOT_GFX_ERROR; tail -n 40 "$LOG"; exit 8
  fi
  sleep 1; i=$((i+1))
done
grep -aq '\[PIPELINE_READY\] state=PLAYING' "$LOG" || { echo BOOT_TIMEOUT; tail -n 30 "$LOG"; exit 8; }

i=0
while [ "$i" -lt 80 ]; do
  if ! demo_owned; then echo AGG_DEAD; tail -n 30 "$LOG"; exit 8; fi
  n=$(grep -ac '\[AGG\]' "$LOG" 2>/dev/null || true); n=${n%%$'\n'*}
  if [ "${n:-0}" -ge "$AGG_NEED" ]; then
    echo "AGG_OK count=$n"
    echo READY_FOR_BROWSER
    trap - EXIT INT TERM
    exit 0
  fi
  sleep 1; i=$((i+1))
done
echo AGG_TIMEOUT
exit 9
