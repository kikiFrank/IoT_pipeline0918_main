#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "${AIBOX_ROOT:-$SCRIPT_DIR/../..}" && pwd)"
OUT="$R/output/light1121"
SNAP="$R/config_snapshots/light1121/pipeline_light1121.txt"
. "$SCRIPT_DIR/../webrtc_start_common.sh" stop
