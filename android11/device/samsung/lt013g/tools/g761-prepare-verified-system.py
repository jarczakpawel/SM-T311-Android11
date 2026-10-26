#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
IMAGE = ROOT / "out/target/product/lt013g/system.img"
HELPER = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G573-FIXED-FINGERPRINT-SYSTEM-20261020/t311_io64_chunk")
HELPER_SHA = "ba964378166bb4263487f85da7ae86df1f597853edde27b892f98acd789c6ff0"
SYSTEM_BYTES = 2235564032
CHUNK_BYTES = 536870912


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for data in iter(lambda: source.read(1048576), b""):
            digest.update(data)
    return digest.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("--qa", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
qa = json.loads(args.qa.read_text())
if (qa.get("result") != "PASS" or qa.get("verified_file_count") != 16
        or qa.get("system_partition_bytes") != SYSTEM_BYTES
        or qa.get("filesystem_clean") is not True
        or Path(qa.get("source", "")).resolve() != IMAGE.resolve()
        or len(qa.get("files", {})) != 16):
    raise SystemExit("G761 BLOCKED: G760 image verification has not passed")
if not IMAGE.is_file() or sha(IMAGE) != qa["image_sha256"]:
    raise SystemExit("G761 BLOCKED: image SHA differs from verified QA")
if not HELPER.is_file() or sha(HELPER) != HELPER_SHA:
    raise SystemExit("G761 BLOCKED: 64-bit TWRP writer is not verified")
if args.out.exists():
    raise SystemExit("G761 BLOCKED: output already exists")
args.out.parent.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix="g761-", dir=args.out.parent) as tmp:
    staging = Path(tmp) / "release"
    staging.mkdir()
    raw = Path(tmp) / "system.raw"
    subprocess.run([str(ROOT / "out/host/linux-x86/bin/simg2img"),
                    str(IMAGE), str(raw)], check=True)
    if raw.stat().st_size != SYSTEM_BYTES:
        raise SystemExit("G761 BLOCKED: wrong raw image length")
    whole = hashlib.sha256()
    parts = []
    with raw.open("rb") as source:
        for i in range(5):
            length = min(CHUNK_BYTES, SYSTEM_BYTES - i * CHUNK_BYTES)
            part_sha = hashlib.sha256()
            archive = staging / f"chunk{i}.raw.gz"
            with archive.open("wb") as compressed:
                with gzip.GzipFile(filename="", mode="wb", fileobj=compressed,
                                   compresslevel=1, mtime=0) as output:
                    remaining = length
                    while remaining:
                        data = source.read(min(1048576, remaining))
                        if not data:
                            raise RuntimeError("G761 truncated input")
                        part_sha.update(data)
                        whole.update(data)
                        output.write(data)
                        remaining -= len(data)
            validation = hashlib.sha256()
            count = 0
            with gzip.open(archive, "rb") as compressed:
                for data in iter(lambda: compressed.read(1048576), b""):
                    validation.update(data)
                    count += len(data)
            if count != length or validation.digest() != part_sha.digest():
                raise RuntimeError(f"G761 compressed part {i} corrupted")
            parts.append({
                "part": i,
                "offset": i * CHUNK_BYTES,
                "raw_bytes": length,
                "raw_sha256": part_sha.hexdigest(),
                "gzip_name": archive.name,
                "gzip_bytes": archive.stat().st_size,
                "gzip_sha256": sha(archive)
            })
    image_raw_sha = whole.hexdigest()
    shutil.copy2(HELPER, staging / "t311_io64_chunk")
    shutil.copy2(args.qa, staging / "IMAGE_QA.json")
    manifest = {
        "device": "SM-T311",
        "result": "VERIFIED_NOT_INSTALLED",
        "image_sha256": qa["image_sha256"],
        "raw_system_sha256": image_raw_sha,
        "raw_system_bytes": SYSTEM_BYTES,
        "chunk_bytes": CHUNK_BYTES,
        "writer_sha256": HELPER_SHA,
        "part_count": len(parts),
        "parts": parts
    }
    (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    staging.rename(args.out)
    print(json.dumps({"status": "G761_VERIFIED_TRANSPORT_READY",
                      "path": str(args.out),
                      "system_sha256": image_raw_sha,
                      "parts": len(parts)}, indent=2))
