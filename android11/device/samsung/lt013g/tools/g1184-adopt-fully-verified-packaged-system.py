#!/usr/bin/env python3
"""G1184: accept exact newly packaged G1182 SYSTEM only after full ext4 audit.

AOSP's target-files packaging re-generates IMAGES/system.img, which differs in
sparse-byte layout from earlier verified product/system.img. Never weaken
G1143's exact-image check. Instead independently verify the packaged SYSTEM
sparse/ext4 (full G1139) before atomically making it the authoritative HOST
system.img and running unmodified G1143 full ZIP CRC and G1152 manifest.
No backup, reboot, Android tablet operations, hidden success or wipe.
"""
import fcntl
import hashlib
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path("/home/debian/android/lineage-18.1-sm-t311")
PRODUCT = ROOT / "out/target/product/lt013g"
QA = Path("/home/debian/Projekty/samsung/linux-7.3-port/tools")
LOCK = Path("/home/debian/Projekty/samsung/artifacts/G1163-android11-rom-finalizer.lock")
ZIP = PRODUCT / "obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-SMT311G1148.zip"
IMAGE = PRODUCT / "system.img"
QA_RECEIPT = PRODUCT / "obj/PACKAGING/G1152-qualified-target-files-qa.json"
MANIFEST = PRODUCT / "obj/PACKAGING/G1152-qualified-ROM-source-manifest.json"


def refuse(message):
    raise SystemExit("G1184_REFUSE: " + message)


def hash_file(path):
    with path.open("rb") as fd:
        return hashlib.file_digest(fd, "sha256").hexdigest()


def fp(contents, key):
    matches = [line.partition("=")[2] for line in contents.decode().splitlines()
               if line.startswith(key + "=")]
    if len(matches) != 1:
        refuse("missing or duplicate property " + key)
    return matches[0]


def main():
    if sys.argv[1:] != ["--g1182-packaging-failure-independently-confirmed"]:
        refuse("require independently observed G1182 target-files QA exit=1")
    if (LOCK.is_symlink() or not LOCK.parent.is_dir()
            or not ZIP.is_file() or ZIP.is_symlink()
            or not IMAGE.is_file() or IMAGE.is_symlink()
            or QA_RECEIPT.exists() or MANIFEST.exists()
            or QA_RECEIPT.is_symlink() or MANIFEST.is_symlink()):
        refuse("unsafe lock, host image or already existing release artifacts")
    with LOCK.open("a+b") as locker:
        try:
            fcntl.flock(locker, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            refuse("another ROM finalizer or OTA generator active")
        if ZIP.stat().st_size < 200_000_000:
            refuse("target-files size implausible")
        with zipfile.ZipFile(ZIP) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or archive.testzip() is not None:
                refuse("target-files ambiguous ZIP members or CRC")
            image_member = "IMAGES/system.img"
            if image_member not in names:
                refuse("packaged canonical system image missing")
            vendor_fp = fp(archive.read("SYSTEM/vendor/build.prop"), "ro.vendor.build.fingerprint")
            system_fp = fp(archive.read("SYSTEM/build.prop"), "ro.system.build.fingerprint")
            product_fp = fp((PRODUCT / "system/build.prop").read_bytes(), "ro.system.build.fingerprint")
            staged_vendor_fp = fp((PRODUCT / "system/vendor/build.prop").read_bytes(),
                                  "ro.vendor.build.fingerprint")
            if (vendor_fp != system_fp or system_fp != product_fp
                    or product_fp != staged_vendor_fp
                    or "SMT311G1148" not in vendor_fp):
                refuse("packaged + staged vendor/system fingerprints disagree")
            fd, filename = tempfile.mkstemp(
                prefix="G1184-candidate-system.", suffix=".img", dir=IMAGE.parent)
            candidate = Path(filename)
            try:
                with os.fdopen(fd, "wb") as target, archive.open(image_member) as source:
                    shutil.copyfileobj(source, target, 1024 * 1024)
                    target.flush()
                    os.fsync(target.fileno())
                if candidate.stat().st_size != archive.getinfo(image_member).file_size:
                    refuse("truncated candidate image")
                # Reuse source-committed full read-only sparse/ext4 e2fsck +
                # all 24 SHA+sizes + no-engineering-app G1139 checks on
                # THIS PACKAGED IMAGE before any host overwrite.
                env = runpy.run_path(
                    str(QA / "g1139-verify-android-system-ext4-image.py"),
                    run_name="g1184_packaged_candidate_validator")
                audit = env["main"]
                audit.__globals__["IMAGE"] = candidate
                audit()
                packaged_sha = hash_file(candidate)
                if packaged_sha != hashlib.file_digest(
                        archive.open(image_member), "sha256").hexdigest():
                    refuse("package system changed after candidate full audit")
                os.chmod(candidate, 0o644)
                os.replace(candidate, IMAGE)
                print("PASS G1184_VERIFIED_PACKAGED_SYSTEM_ATOMICALLY_BECAME_HOST_IMAGE", flush=True)
                print("G1184_CANONICAL_PACKAGED_SYSTEM_SHA=" + packaged_sha, flush=True)
            finally:
                candidate.unlink(missing_ok=True)
        # NO Android source changes, image edits, or unsupported bypasses.
        subprocess.run([sys.executable,
                        str(QA / "g1137-verify-g1130-kernel-android-publication-gate.py"),
                        "--release-gate"], cwd=ROOT, check=True)
        subprocess.run([sys.executable,
                        str(QA / "g1143-verify-target-files-zip-actual-artifacts.py"),
                        str(ZIP), "--all-crc", "--receipt", str(QA_RECEIPT)],
                       cwd=ROOT, check=True)
        subprocess.run([sys.executable,
                        str(ROOT / "device/samsung/lt013g/tools/g1152-write-qualified-release-manifest.py"),
                        "--target-files", str(ZIP),
                        "--qa-receipt", str(QA_RECEIPT),
                        "--output", str(MANIFEST)], cwd=ROOT, check=True)
        print("G1184_PACKAGED_CANONICAL_SYSTEM_AND_TARGETFILES_OFFLINE_RELEASE_PASS (NOT flashed)",
              flush=True)
        print("G1184_MANIFEST_SHA256=" + hash_file(MANIFEST), flush=True)


if __name__ == "__main__":
    main()
