#!/bin/bash
# Start the 12-camera light1121 single-model YOLOX TimeMux pipeline.
set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

aibox_find_root() {
  local d="$1" i=0
  while [ "$i" -le 4 ]; do
    if [ -d "$d/build_unified" ] && [ -d "$d/config_snapshots" ]; then
      (cd "$d" && pwd)
      return 0
    fi
    [ "$d" = "/" ] && break
    d="$(cd "$d/.." 2>/dev/null && pwd)" || break
    i=$((i + 1))
  done
  return 1
}

if [ -n "${AIBOX_ROOT:-}" ] && [ -d "${AIBOX_ROOT}/build_unified" ]; then
  R="$(cd "$AIBOX_ROOT" && pwd)"
else
  R="$(aibox_find_root "$SCRIPT_DIR")" || {
    echo "ROOT_NOT_FOUND: no build_unified/+config_snapshots above $SCRIPT_DIR"
    exit 3
  }
fi
echo "ROOT=$R"

mkdir -p "$R/output/light1121" "$R/runtime_registry"
MODULES_DIR="$R/plugins/ml_modules"
MODULES_LINK="$R/runtime_plugins/ml_modules"
if [ -d "$MODULES_DIR" ]; then
  if [ -L "$MODULES_LINK" ] || [ ! -e "$MODULES_LINK" ]; then
    ln -sfn ../plugins/ml_modules "$MODULES_LINK"
  else
    echo "KEEP_REAL_DIR=$MODULES_LINK"
  fi
  export GST_ML_MODULES_DIR="$MODULES_DIR"
fi

OUT="$R/output/light1121"
SNAP="$R/config_snapshots/light1121/pipeline_light1121.txt"
AGG_NEED=12
LOG="$OUT/light1121_demo.log"

[ -x "$R/build_unified/aibox_stream_pipeline_demo" ] || {
  echo "MISSING_DEMO=$R/build_unified/aibox_stream_pipeline_demo"
  exit 11
}
[ -f "$SNAP" ] || { echo "MISSING_SNAP=$SNAP"; exit 11; }
[ -f "$R/data/models/light1121/yolox_s_light_w8a16.bin" ] || {
  echo "MISSING_MODEL=$R/data/models/light1121/yolox_s_light_w8a16.bin"
  exit 11
}
[ -f "$MODULES_DIR/libml-postprocess-yolox-light1121.so" ] || {
  echo "MISSING_POSTPROCESS_MODULE=$MODULES_DIR/libml-postprocess-yolox-light1121.so"
  exit 11
}

echo "SNAP=$SNAP"
echo "MODEL=$R/data/models/light1121/yolox_s_light_w8a16.bin"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/../webrtc_start_common.sh"
