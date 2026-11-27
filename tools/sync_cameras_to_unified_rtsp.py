#!/usr/bin/env python3
"""Sync Data Manager camera URLs into the split unified RTSP snapshot."""

import argparse
import json
import re
import shutil
import socket
import sys
import tempfile
import uuid
from pathlib import Path
from urllib.parse import urlsplit

from rtsp_supervisor import CONFIG, ROOT, load_split_config, validate


def request(nats_url, subject, body):
    server = urlsplit(nats_url)
    inbox = f"_INBOX.unified_camera_sync.{uuid.uuid4().hex}"
    payload = json.dumps(body).encode()
    with socket.create_connection((server.hostname, server.port or 4222), timeout=5) as sock:
        header = (f'CONNECT {{"verbose":false}}\r\nSUB {inbox} 1\r\n'
                  f"PUB {subject} {inbox} {len(payload)}\r\n")
        sock.sendall(header.encode() + payload + b"\r\n")
        with sock.makefile("rb") as reader:
            while line := reader.readline().split():
                if line[0] == b"PING":
                    sock.sendall(b"PONG\r\n")
                if line[0] == b"MSG":
                    return json.loads(reader.read(int(line[-1])))
    raise RuntimeError(f"No response on {subject}")


def plan_updates(snapshot, config, cameras):
    current = config["cameras"]
    by_url = {row["source"]: row for row in current.values()}
    urls = {row["camera_id"]: row["rtsp_url"] for row in cameras}
    if (len(cameras) != 12 or set(urls) != {f"cam-{i:03}" for i in range(1, 13)}
            or len(set(urls.values())) != 12 or set(urls.values()) != set(by_url)
            or any(not row["enabled"] for row in cameras)):
        raise ValueError("Expected cam-001 through cam-012, enabled, with the same 12 unique RTSP URLs")

    groups = {"ppe": [], "light": [], "scene": []}
    changes = {}
    for cid, url in sorted(urls.items()):
        profile = by_url[url]
        scenes = profile.get("scenes", [])
        kind = "scene" if scenes else profile["cnn"]
        groups[kind].append(cid)
        code = config["cnn"]["profiles"][profile["cnn"]]["algorithm"]["algorithm"]["code"]
        file = Path("streams") / f"{cid}.txt"
        text = (snapshot.parent / file).read_text(encoding="utf-8")
        text, url_count = re.subn(r"(?m)^rtsp_url=.*$", lambda _: f"rtsp_url={url}", text)
        text, algorithm_count = re.subn(r"(?m)^algorithm=.*$", f"algorithm={code}", text)
        if url_count != 1 or algorithm_count != 1:
            raise ValueError(f"{file}: expected one rtsp_url and one algorithm")
        text = re.sub(r"(?m)^scenes=.*\n?", "", text)
        if scenes:
            text = text.rstrip("\n") + "\nscenes=" + ",".join(scenes) + "\n"
        changes[file] = text
    if any(len(ids) != 4 for ids in groups.values()):
        raise ValueError(f"Expected three groups of four cameras: {groups}")
    order = [*groups["ppe"], *groups["light"], *groups["scene"]]
    includes = "".join(f"include=streams/{cid}.txt\n" for cid in order)
    content = snapshot.read_text(encoding="utf-8")
    content, count = re.subn(r"(?m)(?:^include=streams/cam-\d{3}\.txt\n){12}", includes, content)
    if count != 1:
        raise ValueError("Expected one list of 12 stream includes in the unified snapshot")
    changes[Path(snapshot.name)] = content
    return urls, groups, changes


def install_changes(snapshot, changes):
    with tempfile.TemporaryDirectory(prefix="aibox-camera-sync-") as tmp:
        prepared = Path(tmp) / "unified_rtsp"
        shutil.copytree(snapshot.parent, prepared)
        for file, content in changes.items():
            (prepared / file).write_text(content, encoding="utf-8", newline="\n")
        validate(load_split_config(prepared / snapshot.name))
        for file in changes:
            dest = snapshot.parent / file
            temporary = dest.with_suffix(".camera-sync")
            shutil.copyfile(prepared / file, temporary)
            temporary.replace(dest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, default=ROOT / CONFIG)
    parser.add_argument("--apply", action="store_true", help="Write the previewed TXT changes")
    args = parser.parse_args()
    config = load_split_config(args.snapshot)
    validate(config)
    cameras = request(config["runtime"]["nats_url"], "rpc.datamgr.camera.list", {})["cameras"]
    urls, groups, changes = plan_updates(args.snapshot, config, cameras)
    for kind, members in groups.items():
        for cid in members:
            source = urlsplit(urls[cid])
            print(f"{kind:5} {cid}.txt  {source.hostname}:{source.port}{source.path}")
    if args.apply:
        install_changes(args.snapshot, changes)
    print("12 stream files and include order written." if args.apply else "Preview only. Use --apply to write.")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        sys.exit(str(error))
