#!/usr/bin/env python3
"""G963: fail early if current compiled SYSTEM inputs disagree with exact image-QA pins.

Host-only; G755 independently validates the resulting sparse ext4 image,
including mode/ownership/SELinux xattrs. This does not touch the tablet.
"""
import hashlib
from pathlib import Path
import runpy

DEVICE = Path(__file__).resolve().parents[1]
ANDROID = DEVICE.parents[2]
PRODUCT = ANDROID / "out/target/product/lt013g"
EXPECTED = runpy.run_path(str(DEVICE / "tools/g755-verify-systemimage-contents.py"))["EXPECTED"]

if len(EXPECTED) != 16:
    raise SystemExit(f"G963 BLOCKED: expected exactly 16 immutable image paths, got {len(EXPECTED)}")

for path, (expected, mode, uid, gid, label) in EXPECTED.items():
    if path == "/sepolicy":
        artifact = PRODUCT / "root/sepolicy"
    elif path.startswith("/system/"):
        artifact = PRODUCT / "system" / path.removeprefix("/system/")
    else:
        raise SystemExit(f"G963 BLOCKED: unexpected image path {path}")
    if not artifact.is_file() or artifact.is_symlink():
        raise SystemExit(f"G963 BLOCKED: missing or unsafe system input: {path}")
    hasher = hashlib.sha256()
    with artifact.open("rb") as file:
        for chunk in iter(lambda: file.read(1048576), b""):
            hasher.update(chunk)
    if hasher.hexdigest() != expected:
        raise SystemExit(f"G963 BLOCKED: built system input differs from pinned SHA: {path}")
print("G963_HOST_16_OF_16_INPUT_SHA_PASS; strict ext4 image QA remains mandatory; NO FLASH")
