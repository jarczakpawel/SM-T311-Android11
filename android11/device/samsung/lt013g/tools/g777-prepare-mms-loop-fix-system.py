#!/usr/bin/env python3
"""G777: create verified G775 transport, not an installation authorization.

Strictly requires the G772 candidate manifest with its explicit
CANDIDATE_PASS_NOT_INSTALLED state, 17 verified objects, physically pinned
current G730 BOOT+TWRP, full sparse SHA and helper SHA. Builds deterministic
5 raw 512MiB-or-smaller chunks; gzip transports are verified independently.
Does NOT invoke ADB or flash.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[4]
IMAGE = ANDROID / "out/target/product/lt013g/system.img"
HELPER = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G761-SYSTEM-20261022/t311_io64_chunk")
HELPER_SHA = "ba964378166bb4263487f85da7ae86df1f597853edde27b892f98acd789c6ff0"
SPARSE_SHA = "e951b5ce2b52e251f775b57da1335a4c61972ccbcb9aaac2160788f8b2d90782"
SYSTEM_BYTES = 2235564032
CHUNK_BYTES = 536870912
BOOT_SHA = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
RECOVERY_SHA = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for data in iter(lambda: source.read(1048576), b""):
            digest.update(data)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qa", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    qa = json.loads(args.qa.read_text())
    if (qa.get("result") != "CANDIDATE_PASS_NOT_INSTALLED"
            or qa.get("candidate_only") is not True
            or qa.get("hardware_boot_acceptance") is not False
            or qa.get("verified_file_count") != 17
            or len(qa.get("files", {})) != 17
            or qa.get("image_sha256") != SPARSE_SHA
            or qa.get("system_partition_bytes") != SYSTEM_BYTES
            or qa.get("filesystem_clean") is not True
            or Path(qa.get("source", "")).resolve() != IMAGE.resolve()):
        raise SystemExit("G777 BLOCKED: unqualified G775 candidate QA")
    if not IMAGE.is_file() or sha(IMAGE) != SPARSE_SHA:
        raise SystemExit("G777 BLOCKED: candidate image bytes mismatch")
    if not HELPER.is_file() or sha(HELPER) != HELPER_SHA:
        raise SystemExit("G777 BLOCKED: original 64bit writer mismatch")
    if args.out.exists():
        raise SystemExit("G777 BLOCKED: output release directory already exists")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="g773-", dir=args.out.parent) as tmp:
        stage = Path(tmp) / "release"
        stage.mkdir()
        raw = Path(tmp) / "system.raw"
        subprocess.run(
            [str(ANDROID / "out/host/linux-x86/bin/simg2img"), str(IMAGE), str(raw)],
            check=True)
        if raw.stat().st_size != SYSTEM_BYTES:
            raise SystemExit("G777 BLOCKED: raw SYSTEM wrong geometry")
        whole = hashlib.sha256()
        parts = []
        with raw.open("rb") as source:
            for i in range(5):
                length = min(CHUNK_BYTES, SYSTEM_BYTES - i * CHUNK_BYTES)
                part_sha = hashlib.sha256()
                archive = stage / f"chunk{i}.raw.gz"
                with archive.open("wb") as compressed:
                    with gzip.GzipFile(filename="", mode="wb", fileobj=compressed,
                                       compresslevel=1, mtime=0) as output:
                        remaining = length
                        while remaining:
                            data = source.read(min(1048576, remaining))
                            if not data:
                                raise RuntimeError("G777 truncated input")
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
                if count != length or validation.hexdigest() != part_sha.hexdigest():
                    raise RuntimeError(f"G777 chunk {i} mismatch")
                parts.append({
                    "part": i, "offset": i * CHUNK_BYTES, "raw_bytes": length,
                    "raw_sha256": part_sha.hexdigest(),
                    "gzip_name": archive.name,
                    "gzip_bytes": archive.stat().st_size,
                    "gzip_sha256": sha(archive),
                })
        shutil.copy2(HELPER, stage / "t311_io64_chunk")
        shutil.copy2(args.qa, stage / "IMAGE_QA.json")
        manifest = {
            "device": "SM-T311",
            "stage": "G777_MMS_SECURITY_SYSTEM",
            "result": "VERIFIED_CANDIDATE_NOT_INSTALLED",
            "candidate_image_sha256": SPARSE_SHA,
            "image_sha256": SPARSE_SHA,
            "raw_system_sha256": whole.hexdigest(),
            "raw_system_bytes": SYSTEM_BYTES,
            "chunk_bytes": CHUNK_BYTES,
            "writer_sha256": HELPER_SHA,
            "boot_p9_sha256": BOOT_SHA,
            "recovery_p10_sha256": RECOVERY_SHA,
            "part_count": 5,
            "parts": parts,
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        stage.rename(args.out)
    print("G777_TRANSPORT_VERIFIED_NOT_INSTALLED", args.out)
    print("G777_FULL_RAW_SHA", whole.hexdigest())


if __name__ == "__main__":
    main()
