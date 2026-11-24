#!/bin/bash
# Compatibility entry: DRM force-on was replaced by the dedicated service.
set -euo pipefail
sudo -n systemctl start aibox-weston.service
systemctl is-active --quiet aibox-weston.service
echo WESTON_READY
