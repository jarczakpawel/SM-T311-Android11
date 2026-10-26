#!/usr/bin/env python3
"""G994 fail-closed SM-T311 on-device SHA vs Android build release gate.

The built artifact checksum being known and valid does NOT establish that it
matches the running tablet! This gate compares the exact Debian-built files
with a recent, separately ADB-read physical baseline. It never copies files,
flashes a partition, changes USB or probes PIN/PUK.

A PASS in media or ui mode means ONLY 'same bytes as physical baseline',
not that recording, camera or all H264 frames work. The baseline must be
refreshed with actual independent device readback before any real deployment.

No --override, --ignore-radio or --force switches are deliberately provided.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

SHA_RE = re.compile(r"^[a-f0-9]{64}$")
DIR = Path(__file__).resolve().parent
ANDROID = DIR.parents[3]
DEFAULT_MANIFEST = DIR / "g994-physical-snapshot.json"

def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def fail(msg):
    print("G994_BLOCKED", msg, file=sys.stderr)
    return 2

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["media", "ui", "radio", "full-rom"],
                        required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    opt = parser.parse_args()

    if not opt.manifest.is_file():
        return fail("physical manifest missing")
    try:
        manifest = json.loads(opt.manifest.read_text())
    except (OSError, ValueError) as e:
        return fail(f"cannot read physical manifest: {e}")
    if manifest.get("device") != "lt013g" or manifest.get("schema") != 1:
        return fail("wrong model or schema")
    required = {"codec2", "audio", "systemui_apk", "systemui_odex",
                "systemui_vdex", "ril"}
    files = manifest.get("files", [])
    names = [x.get("component") for x in files]
    if len(names) != len(required) or set(names) != required:
        return fail("physical manifest missing or duplicate critical components")
    if not all(SHA_RE.fullmatch(x.get("sha256", "")) for x in files):
        return fail("malformed physical sha256")
    if any(x.get("host", "").startswith("/") or ".." in Path(x.get("host", "")).parts
           or not x.get("tablet", "").startswith("/system/") for x in files):
        return fail("unsafe manifest path")
    for part in ("boot", "recovery"):
        if not SHA_RE.fullmatch(manifest.get(part, {}).get("sha256", "")):
            return fail(part + " physical integrity checksum missing")
    guard = DIR / "g960-source-integrity.py"
    result = subprocess.run([sys.executable, str(guard), "--source", "--boot", "--built"],
                            cwd=ANDROID, capture_output=True, text=True, timeout=65)
    if result.returncode:
        return fail("G960 source/boot/host-build preflight failed: " + result.stderr[-500:])
    print("G994_SOURCE_BUILD_GATE_PASS (not a hardware QA claim)")
    needed = {
        "media": {"codec2", "audio"},
        "ui": {"systemui_apk", "systemui_odex", "systemui_vdex"},
        "radio": {"ril"},
        "full-rom": required,
    }[opt.mode]
    rejected = []
    for spec in files:
        if spec["component"] not in needed:
            continue
        path = ANDROID / spec["host"]
        # release gate must never follow symlinks outside the canonical build output
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(ANDROID):
            rejected.append(spec["component"] + ": host artifact absent/unsafe")
            continue
        actual = checksum(path)
        expected = spec["sha256"]
        if actual != expected:
            rejected.append(spec["component"] + ": BUILD=" + actual[:16] +
                            " TABLET=" + expected[:16])
            print(f"G994_DRIFT {spec['component']} host={actual} physical={expected}")
        else:
            print(f"G994_BYTE_IDENTICAL {spec['component']} sha256={actual}")
    if rejected:
        return fail("RELEASE DENIED (" + opt.mode + "): " + "; ".join(rejected))
    print("G994_PASSED " + opt.mode.upper() +
          " EXACT_BINARY_COMPATIBILITY_ONLY (not a flash authorization)")
    print("G994_REQUIRE_FRESH_LIVE_ADB_BOOT_RECOVERY_CHECKS_BEFORE_ANY_FLASH")
    return 0

if __name__ == "__main__":
    sys.exit(main())
