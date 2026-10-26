#!/usr/bin/env python3
"""Audit whether live SM-T311 device-tree builds are reproducible from Git.

Never copies proprietary blobs or private files. Produces filenames, hashes and
Git status only. Requires no AOSP build or connected tablet.
"""
import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

BLOB_EXT = {".img", ".tar", ".so", ".bin", ".elf", ".fw", ".dat", ".ko", ".o", ".a"}
PATH_RE = re.compile(r'device/samsung/(lt013g|lt01-common)/[A-Za-z0-9_./+~-]+')
BP_RE = re.compile(r'\b(?:src|srcs|init_rc)\s*:\s*(\[[^\]]*\]|"[^"]*")', re.S)
STR_RE = re.compile(r'"([^"]+)"')


def sha256_file(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def tracked(root):
    run = subprocess.run(["git", "-C", str(root), "ls-files", "-z"],
                         capture_output=True, check=True)
    return {Path(s.decode()) for s in run.stdout.split(b"\0") if s}


def active_sources(directory, tracked_files):
    for rel in sorted(tracked_files):
        if rel.suffix not in (".mk", ".bp"):
            continue
        if any(x in rel.name.lower() for x in (".pre-", ".before-", ".bak", ".old")):
            continue
        path = directory / rel
        if path.is_file() and path.stat().st_size < 1000000:
            yield rel, path.read_text(errors="replace")


def inventory(aosp):
    roots = {name: aosp / "device/samsung" / name for name in ("lt013g", "lt01-common")}
    inventory = []
    allowed = {name: tracked(root) for name, root in roots.items()}
    matches = {}
    for name, root in roots.items():
        for rel, content in active_sources(root, allowed[name]):
            # For Makefiles consider all full device-path references. No user
            # uploads, runtime data files or vendor binaries are ever read here.
            if rel.suffix == ".mk":
                for line in content.splitlines():
                    line = line.split("#", 1)[0]
                    for m in PATH_RE.finditer(line):
                        item = m.group().rstrip(".:/")
                        matches.setdefault(item, set()).add(f"{name}/{rel}")
            if rel.suffix == ".bp":
                for m in BP_RE.finditer(content):
                    for source in STR_RE.findall(m.group(1)):
                        if any(ch in source for ch in ("*", "$", ":", "{", "}")):
                            continue
                        local_path = (rel.parent / source)
                        if ".." in local_path.parts:
                            continue
                        item = f"device/samsung/{name}/{local_path.as_posix()}"
                        matches.setdefault(item, set()).add(f"{name}/{rel}")
    for item, users in sorted(matches.items()):
        match = PATH_RE.fullmatch(item)
        if not match:
            continue
        name = match.group(1)
        prefix = f"device/samsung/{name}/"
        relative = Path(item[len(prefix):])
        if ".." in relative.parts:
            continue
        root = roots[name]
        full = root / relative
        if not full.exists():
            inventory.append({"path": item, "status": "MISSING", "referenced_by": sorted(users)})
            continue
        if not full.is_file():
            continue
        stat = full.stat()
        is_tracked = relative in allowed[name]
        if is_tracked:
            continue
        entry = {
            "path": item,
            "status": "UNTRACKED_BUILD_INPUT",
            "bytes": stat.st_size,
            "sha256": sha256_file(full),
            "format": "binary-or-proprietary-review" if full.suffix.lower() in BLOB_EXT
                      or full.name in ("t311-cbd", "wpa_supplicant", "smt311-usb-v115")
                      else "text-or-script-review",
            "referenced_by": sorted(users),
        }
        inventory.append(entry)
    return {
        "device": "SM-T311",
        "status": "INCOMPLETE_PUBLIC_BUILD_INPUTS" if inventory else "ALL_DEVICE_SOURCE_INPUTS_TRACKED",
        "scope": "Makefiles full device paths, Soong bp file sources; source-only no blobs copied",
        "counts": dict(Counter(x["status"] for x in inventory)),
        "inputs": inventory,
        "caution": "No permission to redistribute binary blobs is implied; scrub secrets, confirm provenance and licensing before GitHub.",
    }


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--aosp-root", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    outcome = inventory(args.aosp_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(outcome, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"status": outcome["status"], "counts": outcome["counts"],
                      "manifest_path": str(args.output)}, indent=2))
