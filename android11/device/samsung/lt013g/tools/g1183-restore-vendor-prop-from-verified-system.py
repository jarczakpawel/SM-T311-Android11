#!/usr/bin/env python3
"""G1183: restore ONLY absent generated vendor/build.prop from verified ext4 SYSTEM.

G1148's output cleanup removed vendor/build.prop, while the fully built
read-only SYSTEM image already contains the correct exact vendor property.
This is not a backup or rollback; never edit SYSTEM.img or Android sources.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path("/home/debian/android/lineage-18.1-sm-t311")
PRODUCT = ROOT / "out/target/product/lt013g"
SYSTEM = PRODUCT / "system.img"
DEST = PRODUCT / "system/vendor/build.prop"
SYS_PROP = PRODUCT / "system/build.prop"
SIMG2IMG = ROOT / "out/host/linux-x86/bin/simg2img"


def refuse(reason):
    raise SystemExit("G1183_REFUSE: " + reason)


def value(text, key):
    vals = [line.partition("=")[2] for line in text.splitlines()
            if line.startswith(key + "=")]
    if len(vals) != 1:
        refuse("duplicate or missing property: " + key)
    return vals[0]


def main():
    if (DEST.is_symlink() or DEST.exists() or not DEST.parent.is_dir()
            or DEST.parent.is_symlink()):
        refuse("vendor staging property must be missing; never overwrite")
    if any(not file.is_file() or file.is_symlink() for file in (SYSTEM, SYS_PROP, SIMG2IMG)):
        refuse("SYSTEM source image, tools or system fingerprint missing")
    expected_system_fp = value(SYS_PROP.read_text(), "ro.system.build.fingerprint")
    if ("SMT311G1148" not in expected_system_fp
            or not expected_system_fp.startswith("samsung/lineage_lt013g/lt013g:11/")):
        refuse("expected source-canonical G1148 system fingerprint absent")
    with tempfile.TemporaryDirectory(prefix="g1183-verified-ext4-") as tmp:
        raw = Path(tmp) / "read-only-source-system.raw"
        subprocess.run([str(SIMG2IMG), str(SYSTEM), str(raw)], check=True)
        result = subprocess.run(
            ["/usr/sbin/debugfs", "-R", "cat /vendor/build.prop", str(raw)],
            check=True, capture_output=True)
        data = result.stdout
        if not 100 <= len(data) <= 100_000 or b"\x00" in data:
            refuse("vendor build.prop from image missing or not plain text")
        text = data.decode("utf-8")
        vendor_fp = value(text, "ro.vendor.build.fingerprint")
        if vendor_fp != expected_system_fp:
            refuse("SYSTEM and VENDOR fingerprint mismatch in accepted sparse image")
        stat = subprocess.run(
            ["/usr/sbin/debugfs", "-R", "stat /vendor/build.prop", str(raw)],
            check=True, capture_output=True, text=True)
        if "Inode:" not in stat.stdout or "User:     0" not in stat.stdout:
            refuse("vendor property not present as root-owned in verified EXT4 image")
    # After all read-only validations; atomic CREATE-ONLY file never mutates image.
    fd = os.open(DEST, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    import hashlib
    print("PASS G1183_RESUMED_VENDOR_PROPERTY_BYTE_IDENTICAL_TO_ACCEPTED_EXT4")
    print("VENDOR_BUILD_PROP_SHA256=" + hashlib.sha256(data).hexdigest())
    print("VENDOR_BUILD_PROP_BYTES=" + str(len(data)))


if __name__ == "__main__":
    main()
