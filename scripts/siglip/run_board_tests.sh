#!/bin/bash
set -euo pipefail
S="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DATASET="${1:?usage: run_board_tests.sh reference-dataset sample.jpg}"
JPEG="${2:?usage: run_board_tests.sh reference-dataset sample.jpg}"
[[ -r "$DATASET/data/manifest.jsonl" && -r "$JPEG" ]] || {
  echo "Reference dataset and readable JPEG required" >&2
  exit 2
}
R="${AIBOX_ROOT:-$S}"
BUILD="$S/build-review-siglip"
mkdir -p "$S/output/siglip_worker/tests"
cmake -S "$S" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON >"$S/output/siglip_worker/tests/configure.log"
cmake --build "$BUILD" -j3 --target aibox_siglip_worker \
  frame_cache_policy_test snapshot_service_test pipeline_control_test siglip_worker_core_test \
  ppe_gesture_postprocess_test light_postprocess_test siglip_preprocess_probe siglip_isolated_harness \
  >"$S/output/siglip_worker/tests/build.log" 2>&1
ctest --test-dir "$BUILD" --output-on-failure \
  | tee "$S/output/siglip_worker/tests/ctest.log"
export AIBOX_ROOT="$R"
# shellcheck disable=SC1091
. "$R/scripts/runtime_graphics_env.sh" >"$S/output/siglip_worker/tests/graphics.log"
export GST_PLUGIN_PATH="$R/runtime_plugins"
export GST_ML_MODULES_DIR="$R/plugins/ml_modules"
export GST_REGISTRY="$R/runtime_registry/gst_registry_siglip_test.bin"
python3 "$S/tests/siglip_worker/verify_image_preprocess.py" "$DATASET" --build-dir "$BUILD" \
  | tee "$S/output/siglip_worker/tests/preprocess.log"
bash "$S/tests/siglip_worker/test_jetstream_redelivery.sh" "$JPEG" "$BUILD" \
  | tee "$S/output/siglip_worker/tests/isolated.log"
echo SIGLIP_BOARD_TESTS_OK
