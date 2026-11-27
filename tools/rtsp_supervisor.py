#!/usr/bin/env python3
"""Start and stop the RTSP workers that belong to one local run."""

import argparse
from contextlib import contextmanager
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import tomllib
from urllib.parse import urlsplit

from rtsp_config import load_split_config


ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path("config_snapshots/unified_rtsp/pipeline_unified_rtsp.txt")
AGG = re.compile(r"\[AGG\] camera=(\S+) frame=(\d+).*?cam_total=(\d+)")
SCORE = "[SIGLIP_SCENE_SCORE] "
TERMINAL = {"passed", "stopped", "failed"}
STOP_SECONDS = 25


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def validate(config):
    profiles = config["cnn"]["profiles"]
    for name, profile in profiles.items():
        following = profile.get("then")
        if following is None:
            continue
        if (following == name or following not in profiles
                or profiles[following].get("then") is not None
                or any(p["algorithm"]["algorithm"]["type"] != "single"
                       for p in (profile, profiles[following]))):
            raise ValueError(f"Invalid serial CNN stages for {name}")
    camera_urls = set()
    for camera_id, camera in config["cameras"].items():
        if not re.fullmatch(r"cam-\d+", camera_id):
            raise ValueError(f"Invalid camera ID: {camera_id}")
        source = urlsplit(camera["source"])
        if source.scheme != "rtsp" or not source.hostname or camera["source"] in camera_urls:
            raise ValueError(f"Invalid or repeated RTSP source for {camera_id}")
        camera_urls.add(camera["source"])
        if "cnn" not in camera and "scenes" not in camera:
            raise ValueError(f"Missing CNN or SigLIP2 assignment for {camera_id}")
        if "cnn" in camera:
            profiles[camera["cnn"]]
        if "scenes" not in camera:
            continue
        if "cnn" in camera:
            settings = config["cnn"]["settings"]
            if (settings["timemux"].get("caps_mode") != "canonical_raw"
                    or str(settings["global"]["batch"]) != "1"):
                raise ValueError(f"CNN to SigLIP2 requires canonical raw TimeMux for {camera_id}")
        names = camera["scenes"]
        if not names or len(names) != len(set(names)):
            raise ValueError(f"Missing or repeated scene for {camera_id}")
        scenes = [config["siglip2"]["scenes"][name] for name in names]
        roi = scenes[0]["roi_xyxy"]
        if not (len(roi) == 4 and 0 <= roi[0] < roi[2] <= 1 and 0 <= roi[1] < roi[3] <= 1):
            raise ValueError(f"Invalid ROI for {camera_id}")
        for scene in scenes:
            if scene["roi_xyxy"] != roi:
                raise ValueError(f"Scene prompts must share one ROI for {camera_id}")
            for prompt in (scene["positive"], scene["negative"]):
                if not prompt["text"] or len(prompt["input_ids"]) != 64:
                    raise ValueError(f"Invalid SigLIP2 prompt for {camera_id}")
    if not camera_urls:
        raise ValueError("Configure at least one camera")


def write_ini(path, sections):
    content = "\n\n".join(
        f"[{name}]\n" + "\n".join(f"{key}={value}" for key, value in settings.items())
        for name, settings in sections.items()
    )
    path.write_text(content + "\n", encoding="utf-8")


def rule_fragment(config):
    fragments = []
    scenes = config["siglip2"]["scenes"]
    for camera_id, camera in config["cameras"].items():
        for name in camera.get("scenes", []):
            if name == "workstation_organization":
                fragment = ["[workstation_organization_monitor]"]
            else:
                fragment = ["[[siglip2_scene_monitors]]", f'scene = "{name}"']
            fragment += ["enabled = true", f'camera_id = "{camera_id}"']
            fragment.extend(f"{key} = {json.dumps(value)}" for key, value in scenes[name]["rule"].items())
            fragments.append("\n".join(fragment))
    return "\n\n".join(fragments) + "\n"


def render(config, run):
    runtime = config["runtime"]
    cnn = config["cnn"]
    siglip = config["siglip2"]
    native = run / "config"
    native.mkdir()
    workers = []
    cnn_cameras = {cam: row for cam, row in config["cameras"].items() if "cnn" in row}
    frame_sockets = {cam: run / f"{cam}.shm" for cam, row in cnn_cameras.items()
                     if "scenes" in row}
    for path in frame_sockets.values():
        if len(os.fsencode(path)) >= 108:
            raise ValueError(f"Scene socket path exceeds the Unix socket limit: {path}")

    for camera_id, row in config["cameras"].items():
        if "scenes" not in row:
            continue
        pairs = [siglip["scenes"][name] for name in row["scenes"]]
        destination = native / f"{camera_id}.json"
        write_json(destination, {
            "camera_id": camera_id,
            **({"frame_socket": os.path.relpath(frame_sockets[camera_id], native),
                "frame_caps": cnn["settings"]["timemux"]["group_caps"]}
               if camera_id in frame_sockets else {"rtsp_url": row["source"]}),
            "nats_url": runtime["nats_url"],
            "media_dir": str(ROOT / runtime["media_dir"] / "siglip2"),
            "roi_xyxy": pairs[0]["roi_xyxy"],
            **{key: os.path.relpath(ROOT / siglip[key], native) for key in ("image_encoder", "text_encoder")},
            "scenes": [
                {"scene": name, **{key: pair[key] for key in ("prompt_version", "positive", "negative")}}
                for name, pair in zip(row["scenes"], pairs)
            ],
        })
        workers.append({"name": camera_id, "expected": [f"{camera_id}/{name}" for name in row["scenes"]],
                        "argv": [str(ROOT / runtime["siglip_binary"]), str(destination)]})

    if cnn_cameras:
        settings = deepcopy(cnn["settings"])
        settings["global"]["enabled_streams"] = len(cnn_cameras)
        artifacts = Path(os.path.relpath(run, ROOT))
        settings["pipeline_output"] = {
            "export_execution_plan": 1, "execution_plan_path": artifacts / "execution_plan.json",
            "export_structure_txt": 1, "structure_txt_path": artifacts / "pipeline_structure.txt",
            "runtime_dot_path": artifacts / "pipeline_runtime.dot",
            "runtime_svg_path": artifacts / "pipeline_runtime.svg",
            "runtime_png_path": artifacts / "pipeline_runtime.png",
        }
        algorithms, models = [], []
        exe_dir = (ROOT / runtime["cnn_binary"]).parent
        profiles = cnn["profiles"]
        primary = sorted({row["cnn"] for row in cnn_cameras.values()})
        for key in primary:
            profile = profiles[key]
            algorithm = deepcopy(profile["algorithm"])
            if following := profile.get("then"):
                second = profiles[following]["algorithm"]
                algorithm["algorithm"].update(type="serial", stage_count=2)
                algorithm["stage.2"] = {
                    **second["stage.1"], "stage_index": 1,
                    "input_source": f"stage:{algorithm['stage.1']['stage_id']}",
                    "input_crop": "full", "class_filter": "",
                }
                algorithm["stage.2.pipeline"] = second["stage.1.pipeline"]
            filename = f"{key}-algorithm.txt"
            write_ini(native / filename, algorithm)
            algorithms.append(filename)
        model_profiles = set(primary) | {profiles[key]["then"] for key in primary if "then" in profiles[key]}
        for key in sorted(model_profiles):
            profile = profiles[key]
            model = f"{key}-model.txt"
            labels = profile["labels"]
            label_file = native / f"{key}-labels.txt"
            if labels and isinstance(labels[0], dict):
                label_file = label_file.with_suffix(".json")
                write_json(label_file, labels)
            else:
                label_file.write_text("\n".join(labels) + "\n", encoding="utf-8")
            model_settings = deepcopy(profile["model"])
            model_header = model_settings["model"]
            model_header["model_path"] = os.path.relpath(ROOT / model_header["model_path"], exe_dir)
            model_header["labels_path"] = os.path.relpath(label_file, exe_dir)
            write_ini(native / model, model_settings)
            models.append(model)

        streams = []
        for camera_id, row in cnn_cameras.items():
            algorithm = cnn["profiles"][row["cnn"]]["algorithm"]["algorithm"]["code"]
            filename = f"{camera_id}.txt"
            stream = {"camera_id": camera_id, "name": camera_id, "enabled": 1,
                      "source_type": "rtsp", "rtsp_url": row["source"], "encoding": "h264",
                      **row.get("stream", {})}
            decode = (cnn["decode"][stream["encoding"]] if "decode" in cnn else {
                "depay": "rtph264depay", "parse": "h264parse", "decoder": "v4l2h264dec",
                "output_caps": "video/x-raw,format=NV12",
            })
            write_ini(native / filename, {
                "stream": stream,
                "decode": decode,
                "inference": {"algorithm": algorithm, "frequency": settings["global"]["interval_ms"],
                              "thresh": 0.5, "drop_policy": "latest", **row.get("inference", {})},
            })
            streams.append(filename)
        destination = native / "pipeline.txt"
        write_ini(destination, settings)
        with destination.open("a", encoding="utf-8") as out:
            for section, files in (("streams", streams), ("algorithms", algorithms), ("models", models)):
                out.write(f"\n[{section}]\n" + "".join(f"include={file}\n" for file in files))
        args = [str(ROOT / runtime["cnn_binary"]), "--snapshot", str(destination),
                "--run-forever", "--media-dir", str(ROOT / runtime["media_dir"])]
        for camera_id, path in frame_sockets.items():
            args.extend(["--scene-frame-socket", camera_id, str(path)])
        cnn_worker = {"name": "cnn", "expected": list(cnn_cameras), "argv": args}
        if frame_sockets:
            workers.insert(0, cnn_worker)
        else:
            workers.append(cnn_worker)
    (native / "rule_engine.fragment.toml").write_text(rule_fragment(config), encoding="utf-8")
    return workers


def identity(pid):
    proc = Path(f"/proc/{pid}")
    stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    if stat[0] == "Z":
        raise ProcessLookupError(pid)
    return {"pid": pid, "start": stat[19], "exe": os.readlink(proc / "exe"),
            "boot": Path("/proc/sys/kernel/random/boot_id").read_text().strip()}


def alive(owner):
    if not owner:
        return False
    try:
        return identity(owner["pid"]) == owner
    except (ProcessLookupError, FileNotFoundError):
        return False


def signal_owned(owner, signum):
    try:
        pidfd = os.pidfd_open(owner["pid"])
        try:
            if alive(owner):
                signal.pidfd_send_signal(pidfd, signum)
        finally:
            os.close(pidfd)
    except (ProcessLookupError, FileNotFoundError):
        pass


@contextmanager
def control_lock(directory):
    import fcntl

    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "control.lock").open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def current_run(directory):
    marker = directory / "current.json"
    if marker.exists():
        return directory / load_json(marker)["run"]
    return None


def wait_until(action, seconds, error):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = action()
        if result:
            return result
        time.sleep(0.2)
    raise RuntimeError(error)


def snapshot(run):
    path = run / "state.json"
    return load_json(path) if path.exists() else {}


class Worker:
    def __init__(self, run, spec):
        self.name = spec["name"]
        self.expected = spec["expected"]
        self.ready = False
        self.last = {}
        self.total = {}
        self.reconnects = 0
        self.log = run / f"{self.name}.log"
        self.offset = 0
        with self.log.open("wb") as output:
            self.process = subprocess.Popen(spec["argv"], stdin=subprocess.DEVNULL,
                                            stdout=output, stderr=subprocess.STDOUT)
        self.owner = identity(self.process.pid)

    def poll(self):
        if self.process.poll() is not None:
            raise RuntimeError(f"{self.name} exited with {self.process.returncode}; see {self.log}")
        with self.log.open("rb") as reader:
            reader.seek(self.offset)
            for raw_line in reader:
                if not raw_line.endswith(b"\n"):
                    break
                self.offset += len(raw_line)
                line = raw_line.decode(errors="replace")
                if "[SIGLIP_SCENE_RECONNECT]" in line:
                    self.reconnects += 1
                if "[PIPELINE_READY] state=PLAYING" in line or "[SIGLIP_SCENE_TEXT_READY]" in line:
                    self.ready = True
                match = AGG.search(line)
                if match and self.name == "cnn":
                    self.record(match[1], int(match[3]))
                elif line.startswith(SCORE) and self.name != "cnn":
                    score = json.loads(line[len(SCORE):])
                    key = f"{score['camera_id']}/{score['scene']}"
                    self.record(key, (score["stream_epoch"], score["sample_sequence"]))

    def record(self, key, value):
        if key in self.expected and self.total.get(key) != value:
            self.total[key] = value
            self.last[key] = time.monotonic()

    def missing(self, seconds):
        expiry = time.monotonic() - seconds
        return [key for key in self.expected if self.last.get(key, 0) < expiry or key not in self.last]


def stop_workers(workers):
    for owner in reversed(workers):
        signal_owned(owner, signal.SIGTERM)
    deadline = time.monotonic() + STOP_SECONDS
    while any(alive(owner) for owner in workers) and time.monotonic() < deadline:
        time.sleep(0.2)
    forced = [owner for owner in workers if alive(owner)]
    for owner in forced:
        signal_owned(owner, signal.SIGKILL)
    if forced:
        wait_until(lambda: not any(alive(owner) for owner in forced), 5, "Owned worker did not exit")
    return [owner["pid"] for owner in forced]


def serve(run):
    specs = load_json(run / "workers.json")
    policy = load_json(run / "policy.json")
    state = {"status": "starting", "owner": identity(os.getpid()), "workers": []}
    workers = []
    stopping = False

    def request_stop(_signal, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    write_json(run / "state.json", state)

    def poll_all():
        if stopping:
            raise InterruptedError
        for worker in workers:
            worker.poll()

    def stale():
        return [key for worker in workers for key in worker.missing(
            policy["cnn_progress_timeout_seconds"] if worker.name == "cnn"
            else policy["scene_progress_timeout_seconds"])]

    def ready(worker):
        poll_all()
        return worker.ready and not worker.missing(policy["startup_timeout_seconds"])

    try:
        for spec in specs:
            poll_all()
            worker = Worker(run, spec)
            workers.append(worker)
            state["workers"].append(worker.owner)
            write_json(run / "state.json", state)
            try:
                wait_until(lambda: ready(worker), policy["startup_timeout_seconds"],
                           f"{worker.name} readiness timed out")
            except RuntimeError as error:
                raise RuntimeError(f"{error}; missing={worker.missing(policy['startup_timeout_seconds'])}") from error

        poll_all()
        if stalled := stale():
            raise RuntimeError(f"Streams stalled during startup: {', '.join(stalled)}")
        initial = {key: value for worker in workers for key, value in worker.total.items()}
        started = time.monotonic()
        state["status"] = "ready"
        write_json(run / "state.json", state)
        while True:
            time.sleep(0.2)
            poll_all()
            stalled = stale()
            reconnects = [worker.name for worker in workers if worker.reconnects]
            elapsed = time.monotonic() - started
            if policy["verify_seconds"]:
                if stalled or reconnects:
                    raise RuntimeError(f"Stability check failed after {elapsed:.1f}s; stalled={stalled}; reconnects={reconnects}")
                if elapsed >= policy["verify_seconds"]:
                    final = {key: value for worker in workers for key, value in worker.total.items()}
                    if any(initial[key] == value for key, value in final.items()):
                        raise RuntimeError("A stream did not advance during the verification window")
                    state.update(status="passed", stable_seconds=round(elapsed, 1), start=initial, end=final)
                    break
            else:
                status = "degraded" if stalled else "ready"
                if state["status"] != status or state.get("stalled", []) != stalled:
                    state.update(status=status, stalled=stalled)
                    write_json(run / "state.json", state)
    except InterruptedError:
        state["status"] = "stopped"
    except (OSError, RuntimeError, ValueError) as error:
        state.update(status="failed", error=str(error))
    finally:
        try:
            forced = stop_workers(state["workers"])
            if forced:
                state.update(status="failed", forced_pids=forced)
        except (OSError, RuntimeError) as error:
            state.update(status="failed", error=str(error))
        for worker in workers:
            worker.process.poll()
        write_json(run / "state.json", state)


def preflight(config):
    runtime = config["runtime"]
    profiles = config["cnn"]["profiles"]
    paths = [runtime["cnn_binary"], runtime["siglip_binary"], runtime["plugin_dir"], runtime["ml_modules_dir"],
             config["siglip2"]["image_encoder"], config["siglip2"]["text_encoder"]]
    paths.extend(profile["model"]["model"]["model_path"] for profile in profiles.values())
    for path in paths:
        if not (ROOT / path).exists():
            raise FileNotFoundError(path)
    nats = urlsplit(runtime["nats_url"])
    with socket.create_connection((nats.hostname, nats.port or 4222), timeout=3):
        pass
    annotated = config["cnn"]["settings"]["annotated_media"]
    with socket.socket() as check:
        check.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        check.bind((annotated["bind"], int(annotated["port"])))
    expected = tomllib.loads(rule_fragment(config))
    if expected:
        pid = int(subprocess.check_output([
            "systemctl", "show", "--property=MainPID", "--value", "aibox-rule_engine.service",
        ], text=True))
        if pid <= 0:
            raise RuntimeError("Rule Engine is not running")
        process = Path(f"/proc/{pid}")
        argv = (process / "cmdline").read_bytes().rstrip(b"\0").decode().split("\0")
        path = Path("/etc/aibox/rule_engine.toml")
        for index, arg in enumerate(argv):
            if arg == "--config" and index + 1 < len(argv):
                path = Path(argv[index + 1])
            elif arg.startswith("--config="):
                path = Path(arg.split("=", 1)[1])
        if not path.is_absolute():
            path = (process / "cwd").resolve() / path
        with path.open("rb") as handle:
            loaded = tomllib.load(handle)
        configured = [loaded.get("workstation_organization_monitor", {}),
                      *loaded.get("siglip2_scene_monitors", [])]
        expected_rules = [expected.get("workstation_organization_monitor", {}),
                          *expected.get("siglip2_scene_monitors", [])]
        for rule in filter(None, expected_rules):
            if not any(all(item.get(key) == value for key, value in rule.items()) for item in configured):
                raise RuntimeError(f"Rule Engine config does not match {rule.get('scene', 'workstation_organization')}"
                                   f" for {rule['camera_id']}; see --print-rules and {path}")


def start(config, verification):
    runtime = config["runtime"]
    directory = ROOT / runtime["state_dir"]
    with control_lock(directory):
        previous = current_run(directory)
        if previous:
            state = snapshot(previous)
            if alive(state.get("owner")) or any(alive(item) for item in state.get("workers", [])):
                raise RuntimeError(f"Existing owned run: {previous}")
        preflight(config)
        run = Path(tempfile.mkdtemp(prefix="run-", dir=directory))
        specs = render(config, run)
        write_json(run / "workers.json", specs)
        write_json(run / "policy.json", {**runtime, "verify_seconds": verification})
        environment = {**os.environ, "AIBOX_DETECTION_NATS_URL": runtime["nats_url"],
                       "GST_ML_MODULES_DIR": str(ROOT / runtime["ml_modules_dir"]),
                       "GST_REGISTRY": str(run / "gst-registry.bin")}
        vendor = ROOT / "runtime_vendor_plugins"
        plugins = [str(ROOT / runtime["plugin_dir"])]
        if vendor.is_dir():
            plugins.insert(0, str(vendor))
        environment["GST_PLUGIN_PATH"] = os.pathsep.join(plugins)
        with (run / "supervisor.log").open("wb") as output:
            supervisor = subprocess.Popen([sys.executable, __file__, "--serve", str(run)],
                                          env=environment, stdin=subprocess.DEVNULL, start_new_session=True,
                                          stdout=output, stderr=subprocess.STDOUT)
        wait_until(lambda: snapshot(run) or supervisor.poll() is not None, 5, "Supervisor did not initialize")
        if not snapshot(run):
            raise RuntimeError(f"Supervisor failed to initialize; see {run / 'supervisor.log'}")
        write_json(directory / "current.json", {"run": run.name})
    print(f"RTSP_RUN={run.relative_to(ROOT)}", flush=True)

    def result():
        state = snapshot(run)
        if state.get("status") in TERMINAL or (not verification and state.get("status") == "ready"):
            return state
        if supervisor.poll() is not None:
            raise RuntimeError(f"Supervisor exited; see {run / 'supervisor.log'}")
        return None

    state = wait_until(result, len(specs) * runtime["startup_timeout_seconds"] + verification + STOP_SECONDS + 10,
                       "Supervisor wait expired")
    print(json.dumps(state, indent=2))
    return 0 if state["status"] in {"ready", "passed"} else 1


def stop(config):
    directory = ROOT / config["runtime"]["state_dir"]
    with control_lock(directory):
        run = current_run(directory)
        if run is None:
            print("No owned run")
            return 0
        state = snapshot(run)
        owner = state.get("owner")
        if alive(owner):
            signal_owned(owner, signal.SIGTERM)
            wait_until(lambda: not alive(owner), STOP_SECONDS + 8, "Supervisor did not exit")
        state = snapshot(run)
        forced = stop_workers(state.get("workers", []))
        if forced:
            state.update(status="failed", forced_pids=forced)
            write_json(run / "state.json", state)
        elif state.get("status") not in TERMINAL:
            state.update(status="stopped")
            write_json(run / "state.json", state)
        print(json.dumps(state, indent=2))
        return int(bool(forced or state.get("forced_pids")))


def main():
    os.chdir(ROOT)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("start", "stop"), nargs="?")
    parser.add_argument("--config", type=Path, default=CONFIG,
                        help=f"TXT snapshot entry or legacy JSON (default: {CONFIG})")
    parser.add_argument("--verify-seconds", type=int, default=0,
                        help="Require all workers to stay healthy for this duration, then stop them")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--print-rules", action="store_true")
    parser.add_argument("--serve", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.serve:
        serve(args.serve)
        return 0
    if args.verify_seconds < 0:
        parser.error("--verify-seconds cannot be negative")
    config = load_json(args.config) if args.config.suffix.lower() == ".json" else load_split_config(args.config)
    if args.command == "stop":
        return stop(config)
    validate(config)
    if args.check:
        print(f"Config valid: {len(config['cameras'])} cameras")
    elif args.print_rules:
        print(rule_fragment(config), end="")
    elif args.command == "start":
        return start(config, args.verify_seconds)
    else:
        parser.error("specify start, stop, --check or --print-rules")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        sys.exit(f"RTSP: {error}")
