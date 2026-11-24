#!/bin/bash
# Check the dedicated compositor, not unrelated desktop processes or stale logs.
aibox_graphics_gate_for_start() {
  local sock="$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
  systemctl is-active --quiet aibox-weston.service || {
    echo GFX_FAIL_HEADLESS_WESTON; return 1; }
  [ "$(stat -c %u "$XDG_RUNTIME_DIR" 2>/dev/null)" = "$(id -u)" ] || {
    echo GFX_FAIL_RUNTIME_OWNER; return 1; }
  local i
  for i in 1 2 3 4 5; do
    python3 - "$sock" <<'PY_SOCKET' || return 1
import socket, sys
with socket.socket(socket.AF_UNIX) as client:
    client.settimeout(1)
    client.connect(sys.argv[1])
PY_SOCKET
    systemctl is-active --quiet aibox-weston.service || return 1
    sleep 1
  done
  [ -r /dev/kgsl-3d0 ] && [ -w /dev/kgsl-3d0 ] || {
    echo GFX_FAIL_KGSL_ACCESS; return 1; }
  [ "$__EGL_VENDOR_LIBRARY_FILENAMES" = /usr/share/glvnd/egl_vendor.d/10_adreno.json ] || {
    echo GFX_FAIL_ADRENO_ICD; return 1; }
  echo "GFX_GATE_OK headless-pixman+socket5s+kgsl+adreno-icd"
}
