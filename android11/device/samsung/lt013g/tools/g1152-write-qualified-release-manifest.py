#!/usr/bin/env python3
"""G1152: provenance manifest for a successfully OFFLINE-qualified SM-T311 ROM.

Only run AFTER g1137 release gate + g1139 real SYSTEM ext4 verification and
g1143 real target-files --all-crc --receipt PASS. Reads existing files,
no device access, backup, flash, reboot, SIM, network or GitHub push.
Emits a single atomic JSON receipt labelled HOST-ONLY, never runtime PASS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

AOSP = Path("/home/debian/android/lineage-18.1-sm-t311")
PRODUCT = AOSP / "out/target/product/lt013g"
PROJECT = Path("/home/debian/Projekty/samsung")
BOOT_SHA = "acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565"
RECOVERY_SHA = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
KERNEL_G1130 = "9090d2fa76cb3128542be2a2b3f9bf18ba8288cb"

SOURCE_REPOS = {
    "kernel_publish_branch": PROJECT / "kernel-active-g958",
    "android_device_lt013g": AOSP / "device/samsung/lt013g",
    "android_device_lt01_common": AOSP / "device/samsung/lt01-common",
    "android_hardware_samsung_ril": AOSP / "hardware/samsung/ril",
    "android_frameworks_av": AOSP / "frameworks/av",
    "android_external_v4l2_codec2": AOSP / "external/v4l2_codec2",
    "source_integrity_gates": PROJECT / "linux-7.3-port",
}

def refuse(msg):
    raise SystemExit("G1152_HOST_RELEASE_MANIFEST_REFUSE: " + msg)

def sha256(path):
    if path.is_symlink() or not path.is_file():
        refuse("missing or symlinked input: " + str(path))
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def rev(repo, *args):
    proc = subprocess.run(["git", "-C", str(repo)] + list(args),
                          capture_output=True, text=True, check=False)
    if proc.returncode:
        refuse("cannot resolve pinned repo revision: " + str(repo))
    return proc.stdout.strip()

def fingerprint(name, key):
    path = PRODUCT / "system" / name
    content = path.read_text()
    matches = [line.partition("=")[2].strip() for line in content.splitlines()
               if line.startswith(key + "=")]
    if len(matches) != 1 or not matches[0]:
        refuse("missing/duplicate system/vendor build fingerprint " + str(path))
    return matches[0]

def produce(zip_path, qa_receipt, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    zip_path = zip_path.resolve(strict=True)
    qa_receipt = qa_receipt.resolve(strict=True)
    root = PRODUCT / "obj/PACKAGING/target_files_intermediates"
    if zip_path.parent != root.resolve():
        refuse("ZIP is not in native SM-T311 target-files output directory")
    if not zip_path.name.startswith("lineage_lt013g-target_files-") or not zip_path.name.endswith(".zip"):
        refuse("unexpected target-files ZIP naming")
    if zip_path.stat().st_size < 200_000:
        refuse("target-files ZIP implausibly small")
    receipt = json.loads(qa_receipt.read_text())
    if (receipt.get("status") != "G1143_TARGET_FILES_SHA_RELEASE_PASS" or
            receipt.get("all_crc_checked") is not True or
            receipt.get("recovery_checked") is not True or
            receipt.get("physical_runtime_test_performed") is not False):
        refuse("actual ZIP receipt is not full offline successful QA")
    archive_sha = sha256(zip_path)
    if archive_sha != receipt.get("archive_sha256"):
        refuse("ZIP mutated since fully CRC-checked release receipt")
    if zip_path.name != receipt.get("target_files_name"):
        refuse("QA receipt belongs to a different ZIP")
    sha_system = sha256(PRODUCT / "system.img")
    fields = receipt.get("files", {})
    if (fields.get("system_image", {}).get("sha256") != sha_system or
            fields.get("system_image", {}).get("identical_to_host_verified_system_image") is not True):
        refuse("ZIP SYSTEM image was not proven identical to verified host ext4")
    if sha256(PRODUCT / "boot.img") != BOOT_SHA:
        refuse("BOOT not physically accepted G1130")
    if sha256(PRODUCT / "recovery.img") != RECOVERY_SHA:
        refuse("RECOVERY not physically accepted TWRP")
    refs = {name: rev(path, "rev-parse", "HEAD") for name, path in SOURCE_REPOS.items()}
    pub = rev(SOURCE_REPOS["kernel_publish_branch"],
              "rev-parse", "sm-t311-publish-g1130-verified")
    if pub != KERNEL_G1130:
        refuse("kernel publishing branch moved away from accepted G1130")
    status_flags = {
        name: bool(rev(path, "status", "--porcelain", "--untracked-files=no"))
        for name, path in SOURCE_REPOS.items()
    }
    sysfp = fingerprint("build.prop", "ro.system.build.fingerprint")
    venfp = fingerprint("vendor/build.prop", "ro.vendor.build.fingerprint")
    if sysfp != venfp or "SMT311G1148" not in sysfp:
        refuse("release fingerprint not exact G1148 current build")
    data = {
        "schema": "sm-t311-g1152-source-build-provenance-v1",
        "status": "HOST_OFFLINE_RELEASE_QUALIFIED_NOT_PHYSICALLY_INSTALLED",
        "physical_full_rom_flash_performed": False,
        "physical_full_rom_boot_test_performed": False,
        "hardware_h264_long_run_problem_resolved": False,
        "device": "SM-T311",
        "android": "LineageOS 18.1 / Android 11",
        "kernel": "7.3.0-pjarczak",
        "target_files_filename": zip_path.name,
        "target_files_sha256": archive_sha,
        "host_system_image_sha256": sha_system,
        "boot_image_sha256": BOOT_SHA,
        "twrp_recovery_image_sha256": RECOVERY_SHA,
        "matched_system_vendor_fingerprint": sysfp,
        "native_qa_receipt_filename": qa_receipt.name,
        "native_qa_receipt_sha256": sha256(qa_receipt),
        "native_zip_crc_all_checked": True,
        "native_zip_system_img_equals_host_ext4": True,
        "kernel_publish_branch_commit": pub,
        "source_repo_commits": refs,
        "source_repo_tracked_dirty": status_flags,
        "no_github_push_performed_by_this_tool": True,
        "generated_by": "device/samsung/lt013g/tools/g1152-write-qualified-release-manifest.py",
    }
    with tempfile.NamedTemporaryFile("w", dir=str(output.parent),
                                     prefix="G1152-manifest-", suffix=".tmp",
                                     delete=False) as stream:
        tmp = Path(stream.name)
        json.dump(data, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    try:
        os.replace(tmp, output)
    finally:
        tmp.unlink(missing_ok=True)
    print("PASS G1152_HOST_ONLY_QUALIFIED_OFFLINE_PROVENANCE_NO_DEVICE_FLASH")
    print("G1152_MANIFEST_PATH=" + str(output))
    print("G1152_TARGET_FILES_SHA256=" + archive_sha)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--target-files", type=Path, required=True)
    p.add_argument("--qa-receipt", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    produce(a.target_files, a.qa_receipt, a.output)

if __name__ == "__main__":
    main()
