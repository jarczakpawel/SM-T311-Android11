#!/usr/bin/env python3
"""Prepare independent immutable-code and mutable-overlay payloads for SM-T311 GNSS.

OFFLINE ONLY. This script never mounts, relabels, executes or deploys vendor code.
The outputs contain proprietary Samsung binaries and must NOT be published blindly.
"""
import argparse
import hashlib
import json
import posixpath
import tarfile
from pathlib import Path

IMMUTABLE = ("system/bin/", "system/lib/", "system/usr/",
             "system/vendor/lib/")
EXPECTED_CODE = {"system/bin/gpsd", "system/bin/linker",
                 "system/bin/servicemanager", "system/bin/sensorservice",
                 "system/lib/libc.so", "system/lib/hw/gps.default.so"}


def safe_name(name):
    clean = posixpath.normpath(name)
    if clean == ".":
        return ""
    if clean.startswith("/") or clean == ".." or clean.startswith("../"):
        raise ValueError(f"unsafe archive path {name!r}")
    if "//" in name:
        raise ValueError(f"noncanonical archive path {name!r}")
    return clean.removeprefix("./")


def run(source, out):
    out.mkdir(parents=True, exist_ok=True)
    fixed = out / "lt013g-gps-immutable-code.tar"
    mutable = out / "lt013g-gps-mutable-overlay.tar"
    records = []
    seen = set()
    with (tarfile.open(source, "r") as inp,
          tarfile.open(fixed, "w") as imm,
          tarfile.open(mutable, "w") as dyn):
        for member in inp:
            name = safe_name(member.name)
            if not name:
                continue
            if name in seen:
                raise ValueError(f"duplicate path {name}")
            seen.add(name)
            if not (member.isfile() or member.isdir()):
                raise ValueError(f"unexpected archive member {name} type {member.type}")
            if member.uid not in (0, 1000) or member.gid not in (0, 1000):
                # Only use consistent built-in values instead of leaking source
                # machine names and IDs into filesystem metadata.
                pass
            is_immutable = any(name.startswith(prefix) for prefix in IMMUTABLE)
            archive = imm if is_immutable else dyn
            dest = member.replace(name=name, uid=0, gid=0,
                                  uname="root", gname="root")
            stream = inp.extractfile(member) if member.isfile() else None
            archive.addfile(dest, stream)
            if member.isfile():
                with inp.extractfile(member) as contents:
                    h = hashlib.sha256()
                    while chunk := contents.read(65536):
                        h.update(chunk)
                records.append({"path": name, "immutable": is_immutable,
                                "bytes": member.size, "sha256": h.hexdigest()})
    code = {r["path"] for r in records if r["immutable"]}
    if not EXPECTED_CODE <= code:
        raise ValueError(f"required legacy runtime missing: {sorted(EXPECTED_CODE - code)}")
    if any(r["immutable"] and r["path"] == "system/etc/hosts" for r in records):
        raise ValueError("mutable hosts file must not be immutable")
    manifest = {
        "device": "Samsung SM-T311",
        "purpose": "trusted-immutable legacy stock code vs mutable rootfs config",
        "status": "OFFLINE_OUTPUT_NOT_DEPLOYED",
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "immutable_sha256": hashlib.sha256(fixed.read_bytes()).hexdigest(),
        "mutable_sha256": hashlib.sha256(mutable.read_bytes()).hexdigest(),
        "immutable_tar": fixed.name,
        "mutable_tar": mutable.name,
        "immutable_file_count": sum(1 for r in records if r["immutable"]),
        "immutable_total_bytes": sum(r["bytes"] for r in records if r["immutable"]),
        "mutable_file_count": sum(1 for r in records if not r["immutable"]),
        "mutable_total_bytes": sum(r["bytes"] for r in records if not r["immutable"]),
        "mounting": "NOT IMPLEMENTED, no SELinux executable permission added",
        "contents": records,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    result = run(args.source, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != "contents"},
                     indent=2))
