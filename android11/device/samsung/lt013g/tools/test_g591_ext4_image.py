#!/usr/bin/env python3
"""Verify produced ext4 SYSTEM, not only ZIP tree; no device writes."""
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile

if len(sys.argv) != 2:
    raise SystemExit("usage: test_g591_ext4_image.py /path/to/G591-target_files.zip")
archive = Path(sys.argv[1]).resolve()
assert archive.name.endswith("target_files-SMT311G591.zip")
root = Path(__file__).resolve().parents[4]
simg2img = root / "out/host/linux-x86/bin/simg2img"
e2fsck = root / "out/host/linux-x86/bin/e2fsck"
debugfs = Path("/usr/sbin/debugfs")
assert all(p.is_file() for p in (archive, simg2img, e2fsck, debugfs))
with tempfile.TemporaryDirectory(prefix="g591-ext4-") as tmp:
    temp = Path(tmp)
    sparse = temp / "system-sparse.img"
    raw = temp / "system-raw.img"
    extracted = temp / "gps.default.so"
    with ZipFile(archive) as z:
        members = set(z.namelist())
        assert "IMAGES/system.img" in members
        assert "SYSTEM/vendor/lib/hw/gps.default.so" in members
        assert not any("T311GnssStatus" in k for k in members)
        expected = hashlib.sha256(z.read("SYSTEM/vendor/lib/hw/gps.default.so")).hexdigest()
        with z.open("IMAGES/system.img") as src, sparse.open("wb") as dest:
            shutil.copyfileobj(src, dest, length=16*1024*1024)
    subprocess.run([str(simg2img), str(sparse), str(raw)], check=True)
    print("G591_EXT4_REAL_SYSTEM_BYTES", raw.stat().st_size, flush=True)
    with raw.open("rb") as image_file:
        print("G591_EXT4_RAW_SYSTEM_SHA256",
              hashlib.file_digest(image_file, "sha256").hexdigest(), flush=True)
    check = subprocess.run([str(e2fsck), "-fn", str(raw)], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print("G591_EXT4_E2FSCK_EXIT", check.returncode, flush=True)
    if check.returncode not in (0,):
        print(check.stdout[-5000:])
        raise SystemExit("G591_EXT4_E2FSCK_FAIL")
    read = subprocess.run([str(debugfs), "-R",
                           "dump /vendor/lib/hw/gps.default.so " + str(extracted),
                           str(raw)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if read.returncode or not extracted.is_file():
        print(read.stdout)
        raise SystemExit("G591_EXT4_MISSING_GPS_DEFAULT")
    physical = hashlib.sha256(extracted.read_bytes()).hexdigest()
    if physical != expected:
        raise SystemExit("G591_EXT4_WRONG_GPS_HAL")
    print("G591_EXT4_GPS_HAL_MATCH_SHA256", physical, flush=True)
    # Prove exact new Codec2 private-memfd policy is in the REAL ext4,
    # not just in the sparse ZIP's individual SYSTEM/ files.
    policy_extracted = temp / "vendor_sepolicy.cil"
    with ZipFile(archive) as z:
        policy_sha = hashlib.sha256(
            z.read("SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil")).hexdigest()
    pol = subprocess.run([str(debugfs), "-R",
                          "dump /vendor/etc/selinux/vendor_sepolicy.cil " + str(policy_extracted),
                          str(raw)], text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if pol.returncode or not policy_extracted.is_file():
        raise SystemExit("G591_EXT4_SEPOLICY_MISSING")
    policy = policy_extracted.read_text()
    rules = ("(type mediacodec_tmpfs)",
             "(typetransition mediacodec tmpfs_30_0 file mediacodec_tmpfs)",
             "(allow mediacodec mediacodec_tmpfs (file (read write getattr map)))")
    assert all(rule in policy for rule in rules)
    assert hashlib.sha256(policy_extracted.read_bytes()).hexdigest() == policy_sha
    print("G591_EXT4_CODEC2_PRIVATE_MEMFD_RULES_AND_SHA_PASS", flush=True)
    for entry in ("/product/app/T311GnssStatus", "/product/app/T311GnssStatus/T311GnssStatus.apk",
                  "/product/app/T311Camera2Smoke", "/product/app/T311Camera2Smoke/T311Camera2Smoke.apk"):
        chk = subprocess.run([str(debugfs), "-R", "stat " + entry, str(raw)],
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if "File not found" not in chk.stdout:
            print(chk.stdout)
            raise SystemExit("G591_EXT4_DIAGNOSTIC_APP_PRESENT_OR_NOT_PROVED_ABSENT")
    print("G591_EXT4_DIAGNOSTIC_APP_ABSENT_PASS", flush=True)
print("G591_EXT4_ALL_CHECKS_PASS")
