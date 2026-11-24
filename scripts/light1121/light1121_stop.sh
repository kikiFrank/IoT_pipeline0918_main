#!/bin/bash
# Same SIGTERM/NULL/VPU shutdown as the shared CNN launcher.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export AIBOX_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
exec bash "$SCRIPT_DIR/../yolo_x_stacklight_stop.sh"
