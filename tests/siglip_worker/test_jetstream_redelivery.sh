#!/bin/bash
# Board-only test. Uses a temporary JetStream server and mock RPC subjects.
set -euo pipefail
S="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
JPEG="${1:?usage: test_jetstream_redelivery.sh sample.jpg [build-directory]}"
BUILD="${2:-$S/build-review-siglip}"
R="${AIBOX_ROOT:-$S}"
BIN="$BUILD/aibox_siglip_worker"
HARNESS="$BUILD/siglip_isolated_harness"
PORT="${SIGLIP_TEST_NATS_PORT:-45223}"
URL="nats://127.0.0.1:$PORT"
[[ -r "$JPEG" && -x "$BIN" && -x "$HARNESS" ]] || {
  echo "Readable JPEG and built worker/harness required" >&2
  exit 2
}
OUT="$S/output/siglip_worker"
mkdir -p "$OUT"
T="$(mktemp -d "$OUT/isolated_test.XXXXXX")"
mkdir -p "$T/media" "$T/outbox" "$T/failures" "$T/logs"
cp -- "$JPEG" "$T/media/test.jpg"
server_pid=""; mock_pid=""; worker_pid=""
cleanup() {
  for pid in "$worker_pid" "$mock_pid" "$server_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT
nats-server -js -a 127.0.0.1 -p "$PORT" -sd "$T/nats-store" >"$T/nats.log" 2>&1 & server_pid=$!
ready=0
for ((i=0;i<30;i++)); do
  kill -0 "$server_pid" 2>/dev/null || { cat "$T/nats.log" >&2; exit 1; }
  if grep -Fq 'Server is ready' "$T/nats.log" && "$HARNESS" setup "$URL" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
done
[[ "$ready" == 1 ]]
"$HARNESS" mock "$URL" "$T/media/test.jpg" >"$T/mock.log" 2>&1 & mock_pid=$!
for ((i=0;i<30;i++)); do
  if grep -Fq MOCK_READY "$T/mock.log"; then break; fi
  sleep 1
done
grep -Fq MOCK_READY "$T/mock.log"
python3 - "$S" "$T" "$URL" <<'PY'
import pathlib, sys
s, t = map(pathlib.Path, sys.argv[1:3])
cfg = (s / 'config_snapshots/siglip_worker/worker.txt').read_text(encoding='utf-8')
changes = {
    'url': sys.argv[3],
    'stream': 'ALARM_READY_SIGLIP_ISOLATED',
    'subject': 'test.siglip.ready',
    'durable': 'siglip-worker-isolated',
    'media_meta_rpc': 'test.siglip.rpc.media',
    'alarm_list_rpc': 'test.siglip.rpc.alarm',
    'ack_wait_ms': '10000',
    'in_progress_interval_ms': '2000',
    'max_file_wait_ms': '1000',
    'infer_timeout_ms': '1000',
    'model_path': str(s / 'data/models/siglip2/image_encoder.bin'),
    'htp_device_id': str(int(pathlib.Path('/dev/fastrpc-cdsp1').exists())),
    'allowed_media_root': str(t / 'media'),
    'outbox_dir': str(t / 'outbox'),
    'failure_dir': str(t / 'failures'),
    'log_dir': str(t / 'logs'),
}
lines = []
for line in cfg.splitlines():
    key, sep, _ = line.partition('=')
    lines.append(f'{key}={changes.pop(key)}' if sep and key in changes else line)
if changes:
    raise SystemExit(f'missing test config keys: {sorted(changes)}')
(t / 'worker_test.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
PY
export AIBOX_ROOT="$R"
# shellcheck disable=SC1091
. "$R/scripts/runtime_graphics_env.sh" >"$T/graphics.log"
export GST_PLUGIN_PATH="$R/runtime_plugins"
export GST_ML_MODULES_DIR="$R/plugins/ml_modules"
export GST_REGISTRY="$R/runtime_registry/gst_registry_siglip_isolated.bin"
"$BIN" --config "$T/worker_test.txt" --prepare-consumer >"$T/prepare.log"
SIGLIP_TEST_CRASH_AFTER_OUTBOX=1 "$BIN" --config "$T/worker_test.txt" >"$T/worker_crash.log" 2>&1 & worker_pid=$!
for ((i=0;i<45;i++)); do
  if grep -Fq '[SIGLIP_READY] state=PLAYING consumer=READY model=AWAITING_FIRST_INFERENCE' "$T/worker_crash.log"; then break; fi
  kill -0 "$worker_pid" 2>/dev/null || { tail -30 "$T/worker_crash.log"; exit 1; }
  sleep 1
done
grep -Fq '[SIGLIP_READY] state=PLAYING consumer=READY model=AWAITING_FIRST_INFERENCE' "$T/worker_crash.log"
"$HARNESS" publish "$URL"
set +e
wait "$worker_pid"; crash_code=$?
set -e
worker_pid=""
[ "$crash_code" -eq 90 ] || { echo "EXPECTED_CRASH_90 actual=$crash_code"; exit 1; }
[ "$(find "$T/outbox" -name '*.json' | wc -l)" -eq 1 ]
rm "$T/media/test.jpg"
"$BIN" --config "$T/worker_test.txt" >"$T/worker_recovery.log" 2>&1 & worker_pid=$!
for ((i=0;i<50;i++)); do
  if grep -Fq '[SIGLIP_REDELIVERY_ACK]' "$T/worker_recovery.log"; then break; fi
  kill -0 "$worker_pid" 2>/dev/null || { tail -30 "$T/worker_recovery.log"; exit 1; }
  sleep 1
done
grep -Fq '[SIGLIP_REDELIVERY_ACK]' "$T/worker_recovery.log"
kill -TERM "$worker_pid"
wait "$worker_pid"; worker_pid=""
grep -Fq '[SIGLIP_SHUTDOWN_OK] state=NULL' "$T/worker_recovery.log"
"$HARNESS" status "$URL" | tee "$T/status.log"
grep -Fq 'ACK_FLOOR=1' "$T/status.log"
# Publish while stopped: durable must retain a new alarm sharing the same JPEG.
cp -- "$JPEG" "$T/media/test.jpg"
"$HARNESS" publish "$URL" test-alarm-2
"$HARNESS" status "$URL" | tee "$T/stopped_status.log"
grep -Fq 'PENDING=1' "$T/stopped_status.log"
"$BIN" --config "$T/worker_test.txt" >"$T/worker_backlog.log" 2>&1 & worker_pid=$!
for ((i=0;i<30;i++)); do
  if grep -Fq '[SIGLIP_COMMITTED] alarm=test-alarm-2 ' "$T/worker_backlog.log"; then break; fi
  sleep 1
done
grep -Fq '[SIGLIP_COMMITTED] alarm=test-alarm-2 ' "$T/worker_backlog.log"
"$HARNESS" bad "$URL"
for ((i=0;i<20;i++)); do
  if grep -Fq 'stage=PARSE_READY' "$T/worker_backlog.log"; then break; fi
  sleep 1
done
grep -Fq 'stage=PARSE_READY' "$T/worker_backlog.log"
# A failed outbox write must leave the delivery unacknowledged.
chmod 500 "$T/outbox"
"$HARNESS" publish "$URL" test-alarm-3
set +e
wait "$worker_pid"; disk_code=$?
set -e
worker_pid=""
chmod 700 "$T/outbox"
[ "$disk_code" -ne 0 ] || { echo EXPECTED_WRITE_FAILURE; exit 1; }
"$HARNESS" status "$URL" | tee "$T/disk_failure_status.log"
grep -Fq 'ACK_FLOOR=3' "$T/disk_failure_status.log"
"$BIN" --config "$T/worker_test.txt" >"$T/worker_disk_recovery.log" 2>&1 & worker_pid=$!
for ((i=0;i<30;i++)); do
  if grep -Fq '[SIGLIP_COMMITTED] alarm=test-alarm-3 ' "$T/worker_disk_recovery.log"; then break; fi
  sleep 1
done
grep -Fq '[SIGLIP_COMMITTED] alarm=test-alarm-3 ' "$T/worker_disk_recovery.log"
kill -TERM "$worker_pid"
wait "$worker_pid"; worker_pid=""
"$HARNESS" status "$URL" | tee "$T/final_status.log"
grep -Fq 'ACK_FLOOR=4' "$T/final_status.log"
python3 - "$T" <<'PY'
import hashlib, json, pathlib, sys
t = pathlib.Path(sys.argv[1]); files = list((t/'outbox').glob('*.json'))
assert len(files) == 3
records = [json.loads(f.read_text()) for f in files]
o = next(r for r in records if r['alarm_id'] == 'test-alarm')
assert o['alarm_id'] == 'test-alarm' and o['event_id'] == 'test-asset'
assert o['source_event_id'] == 'source-test' and o['camera_id'] == 'cam-test'
assert o['event_timestamp_ms'] == 1793180000000
assert len(o['embedding']) == 768
assert o['image_sha256'] == hashlib.sha256((t/'media/test.jpg').read_bytes()).hexdigest()
assert len({r['image_sha256'] for r in records}) == 1
failures = [json.loads(f.read_text()) for f in (t/'failures').glob('*.json')]
assert len(failures) == 1 and failures[0]['stage'] == 'PARSE_READY'
print('ISOLATED_SIGLIP_CHAIN_OK')
PY
