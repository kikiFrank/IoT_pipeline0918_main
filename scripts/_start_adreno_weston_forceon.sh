#!/bin/bash
# Start the dedicated headless compositor only when it is inactive.
set -euo pipefail
if ! systemctl is-active --quiet aibox-weston.service; then
  echo "GFX_WESTON_START: aibox-weston.service"
  sudo -n systemctl start aibox-weston.service
fi
systemctl is-active --quiet aibox-weston.service
echo WESTON_READY
