"""Read the unified RTSP snapshot and its relative includes."""

import csv
import tomllib


def read_sections(path):
    sections = {}
    section = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            name = line[1:-1]
            if name in sections:
                raise ValueError(f"{path}:{number}: repeated section [{name}]")
            section = sections[name] = {}
            continue
        key, separator, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if section is None or not separator or not key:
            raise ValueError(f"{path}:{number}: expected section or key=value")
        if key == "include":
            section.setdefault(key, []).append(value)
        elif key in section:
            raise ValueError(f"{path}:{number}: repeated key {key}")
        else:
            section[key] = value
    return sections


def read_model(path):
    model = read_sections(path)
    header = model["model"]
    labels_path = path.parent / header.pop("labels_path")
    if header.pop("labels_format", "text") == "colored_csv":
        with labels_path.open(encoding="utf-8", newline="") as handle:
            labels = [{"id": int(row["id"]), "label": row["label"], "color": row["color"]}
                      for row in csv.DictReader(handle)]
    else:
        labels = labels_path.read_text(encoding="utf-8").splitlines()
    return model, labels


def read_scene(path):
    sections = read_sections(path)
    header = sections["scene"]
    scene = {"roi_xyxy": [float(value) for value in header["roi_xyxy"].split(",")],
             "prompt_version": header["prompt_version"]}
    for side in ("positive", "negative"):
        prompt = sections[side]
        scene[side] = {"text": prompt["text"],
                       "input_ids": [int(value) for value in prompt["input_ids"].split(",")]}
    scene["rule"] = tomllib.loads("\n".join(f"{key} = {value}" for key, value in sections["rule"].items()))
    return header["name"], scene


def load_split_config(path):
    root = read_sections(path)

    def includes(section):
        return (path.parent / filename for filename in root[section]["include"])

    models = {}
    for child in includes("models"):
        model, labels = read_model(child)
        models[model["model"]["model_id"]] = (model, labels)

    profiles = {}
    for child in includes("algorithms"):
        algorithm = read_sections(child)
        header = algorithm["algorithm"]
        following = header.pop("then", None)
        model, labels = models[algorithm["stage.1"]["model_id"]]
        profile = {"algorithm": algorithm, "model": model, "labels": labels}
        if following is not None:
            profile["then"] = following
        profiles[child.stem] = profile

    profile_by_code = {profile["algorithm"]["algorithm"]["code"]: key
                       for key, profile in profiles.items()}
    for profile in profiles.values():
        if "then" in profile:
            profile["then"] = profile_by_code[profile["then"]]

    cameras = {}
    for child in includes("streams"):
        sections = read_sections(child)
        stream, inference = sections["stream"], sections["inference"]
        camera_id = stream.pop("camera_id")
        if stream.get("enabled", "1") == "0":
            continue
        if camera_id in cameras:
            raise ValueError(f"{child}: repeated camera ID {camera_id}")
        camera = {"source": stream.pop("rtsp_url"), "stream": stream, "inference": inference}
        if "algorithm" in inference:
            camera["cnn"] = profile_by_code[inference.pop("algorithm")]
        if "scenes" in inference:
            camera["scenes"] = [name.strip() for name in inference.pop("scenes").split(",")]
        cameras[camera_id] = camera

    encoder_path, = includes("siglip2")
    siglip = read_sections(encoder_path)["model"]
    siglip["scenes"] = dict(read_scene(child) for child in includes("scenes"))
    runtime = root["runtime"]
    for key in ("startup_timeout_seconds", "cnn_progress_timeout_seconds", "scene_progress_timeout_seconds"):
        runtime[key] = int(runtime[key])
    settings = {name: root[name] for name in ("global", "timemux", "annotated_media", "frame_cache")}
    decode = {name.removeprefix("decode."): values for name, values in root.items()
              if name.startswith("decode.")}
    return {"runtime": runtime, "cnn": {"settings": settings, "profiles": profiles, "decode": decode},
            "siglip2": siglip, "cameras": cameras}
