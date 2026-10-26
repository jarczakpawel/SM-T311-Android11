#!/usr/bin/env python3
"""G776: read-only *candidate-only* ext4 QA for the G775 MMS hardening rebuild.

Retain ALL 14 unchanged physical G761 file pins and only permit precisely the
two necessary bootclasspath-dependent odex rebuilds, plus a pinned new
framework.jar. This is not an installer authorization or a claim of test PASS
on physical hardware; result must never be 'PASS' in G761 flash manifests.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import tempfile
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[4]
QA_SOURCE = Path(__file__).resolve().with_name("g755-verify-systemimage-contents.py")
spec = importlib.util.spec_from_file_location("g755_source_qa", QA_SOURCE)
assert spec is not None and spec.loader is not None
g755 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g755)

BASE = dict(g755.EXPECTED)
# The two expected changes are confirmed in host output. No wildcard hashes.
BASE["/system/priv-app/MediaProvider/oat/arm/MediaProvider.odex"] = (
    "e5b23ad85fb1b0fe60c7c933acec5384685c721d1ed4738721c50454aa10252e",
    "0644", 0, 0, "u:object_r:system_file:s0"
)
BASE["/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex"] = (
    "dba8003434815b7bd98d3edca94cfe3a9d947786f8564c88eff2dfd3bf2c047a",
    "0644", 0, 0, "u:object_r:system_file:s0"
)
BASE["/system/framework/framework.jar"] = (
    "a9c8971a9cb48d853070ad90df1678659774c533afec9c2d0fe818404f0fb34b",
    "0644", 0, 0, "u:object_r:system_file:s0"
)
IMAGE_EXPECT = "e951b5ce2b52e251f775b57da1335a4c61972ccbcb9aaac2160788f8b2d90782"
assert len(BASE) == 17
assert len(set(BASE)-set(g755.EXPECTED)) == 1


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for part in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    image = ANDROID / "out/target/product/lt013g/system.img"
    if not image.is_file() or digest(image) != IMAGE_EXPECT:
        raise RuntimeError("G776 reject: unrecognized or changed candidate SYSTEM")
    with tempfile.TemporaryDirectory(prefix="g772-readonly-image-") as temp:
        raw = Path(temp) / "system.raw"
        g755.run(ANDROID / "out/host/linux-x86/bin/simg2img", image, raw)
        if raw.stat().st_size != 2235564032:
            raise RuntimeError("G776 reject: p20 partition geometry mismatch")
        sb = g755.run("/usr/sbin/dumpe2fs", "-h", raw).stdout.decode()
        count = re.search(r"(?m)^Block count:\s+(\d+)$", sb)
        size = re.search(r"(?m)^Block size:\s+(\d+)$", sb)
        state = re.search(r"(?m)^Filesystem state:\s+(\S+)$", sb)
        if (not count or not size or not state
                or int(count[1]) * int(size[1]) != 2235564032
                or state[1] != "clean"):
            raise RuntimeError("G776 reject: ext4 filesystem invalid")
        old = g755.EXPECTED
        try:
            g755.EXPECTED = BASE
            root, files = g755.verify(raw, g755.ROOTS)
        finally:
            g755.EXPECTED = old
        if len(files) != 17:
            raise RuntimeError("G776 reject: not all 17 pinned files found")
    report = {
        "result": "CANDIDATE_PASS_NOT_INSTALLED",
        "device": "lt013g",
        "image_sha256": IMAGE_EXPECT,
        "system_partition_bytes": 2235564032,
        "verified_file_count": len(files),
        "filesystem_clean": True,
        "source": str(image),
        "root_layout": root,
        "candidate_only": True,
        "hardware_boot_acceptance": False,
        "previous_g761_release_not_modified": True,
        "files": files
    }
    if args.out.exists():
        raise RuntimeError("G776 reject: refusing to overwrite report")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print("G776_CANDIDATE_QA_PASS_NOT_FLASHABLE; 17/17 exact SHA/DAC/SELinux")
    print("IMAGE", IMAGE_EXPECT)
    print("REPORT", args.out)


if __name__ == "__main__":
    main()
