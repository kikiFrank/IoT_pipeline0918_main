#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "${AIBOX_ROOT:-$SCRIPT_DIR/../..}" && pwd)"
OUT="$R/output/ppe_gesture1121"
SNAP="$R/config_snapshots/ppe_gesture1123/pipeline_ppe_gesture1121.txt"
. "$SCRIPT_DIR/../webrtc_start_common.sh" stop
