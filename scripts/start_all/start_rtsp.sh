#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
case "${1:-}" in
  --check|--print-rules|--help|-h)
    exec python3 tools/rtsp_supervisor.py "$@"
    ;;
esac

export AIBOX_ROOT="$PWD"
bash scripts/_start_adreno_weston_forceon.sh
source scripts/runtime_graphics_env.sh
source scripts/aibox_graphics_gate.sh
aibox_graphics_gate_for_start

ulimit -n 65536 2>/dev/null || true
export AIBOX_WEBRTC_SPLITTER=demux
export AIBOX_GFX_EINVAL_WARN="${AIBOX_GFX_EINVAL_WARN:-1}"
export GST_DEBUG="${GST_DEBUG:-0}"
exec python3 tools/rtsp_supervisor.py start "$@"
