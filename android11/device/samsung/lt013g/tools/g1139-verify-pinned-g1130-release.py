#!/usr/bin/env python3
"""G1139 immutable G1130 release BOOT guard, no ADB, backup, or flash.

Both --source and --boot are required in the Android custom image recipe.
Check the exact known working G1130 commit and Android 8MiB BOOT with zImage
and appended Exynos DTB. This is an image-input check, NOT hardware testing.
"""
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

OPTIONS = {"--source", "--boot", "--built"}
options = set(sys.argv[1:])
if not options or options - OPTIONS:
    sys.exit("G1139_USAGE --source --boot [--built]")
DEVICE = Path(__file__).resolve().parents[1]
ANDROID = DEVICE.parents[2]
PROJECT = Path("/home/debian/Projekty/samsung")
KERNEL = PROJECT / "kernel-active-g958"
BOOT = DEVICE / "prebuilt/BOOT-7.3.0-pjarczak-G1130-VERIFIED-8MiB.img"
ZIMAGE = DEVICE / "prebuilt/kernel-7.3.0-pjarczak-G1130-VERIFIED-zImage.bin"
RECOVERY = DEVICE / "prebuilt/recovery-current.img"
BUILT = ANDROID / "out/target/product/lt013g/boot.img"
G958 = "0d7c924cae1d70ac8015a150e432295a16a6961a"
G1129 = "ea732082732014634a2b7802910d36105b1515e1"
G1130 = "9090d2fa76cb3128542be2a2b3f9bf18ba8288cb"
BOOT_SHA = "acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565"
REC_SHA = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
ZIMAGE_SHA = "4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6"
DTB_SHA = "6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb"
CHANGED_FILES = {
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc.c",
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc_common.h",
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc_enc.c",
}

def refuse(message):
    sys.exit("G1139_REFUSE: " + message)

def git(*args):
    try:
        return subprocess.check_output(["git", "-C", str(KERNEL), *args],
                                       text=True, stderr=subprocess.PIPE).strip()
    except subprocess.CalledProcessError:
        refuse("G1130 git history missing (local source checkout required)")

def sha(payload):
    return hashlib.sha256(payload).hexdigest()

def check_file(path, expected, length=8388608):
    if not path.is_file() or path.stat().st_size != length:
        refuse("missing/invalid image input " + str(path))
    if sha(path.read_bytes()) != expected:
        refuse("mismatched SHA on " + str(path))

if "--source" in options:
    if git("rev-parse", "sm-t311-publish-g1130-verified") != G1130:
        refuse("publication branch must be EXACT G1130, never G958 or G1131")
    if git("rev-parse", G1130 + "^") != G1129 or git("rev-parse", G1129 + "^") != G958:
        refuse("G1130 kernel history changed")
    changes = set(git("diff", "--name-only", G958, G1130).splitlines())
    if changes != CHANGED_FILES:
        refuse("unexpected G1130 source differences " + repr(changes))
    mfc = git("show", G1130 + ":" + "drivers/media/platform/samsung/s5p-mfc/s5p_mfc.c")
    if "G1130_MFC_RECORD_QOS" not in mfc or "G1131 CPU policy QoS" in mfc:
        refuse("G1130 PM-QoS source missing or experimental G1131 included")
    print("G1139_SOURCE_PASS_G1130_EXACT " + G1130)

if "--boot" in options:
    check_file(BOOT, BOOT_SHA)
    check_file(RECOVERY, REC_SHA)
    data = BOOT.read_bytes()
    if data[:8] != b"ANDROID!":
        refuse("not Android BOOT image")
    kernel_size = struct.unpack_from("<I", data, 8)[0]
    ramdisk_size = struct.unpack_from("<I", data, 16)[0]
    page_size = struct.unpack_from("<I", data, 36)[0]
    if (kernel_size, ramdisk_size, page_size) != (6473529, 786423, 2048):
        refuse("G1130 physical BOOT partition/header layout changed")
    zimage = data[page_size:page_size + 6387040]
    check_file(ZIMAGE, ZIMAGE_SHA, 6387040)
    dtb = data[page_size + 6387040:page_size + kernel_size]
    if zimage != ZIMAGE.read_bytes():
        refuse("standalone release zImage differs from physical BOOT")
    config = (DEVICE / "BoardConfig.mk").read_text()
    if "TARGET_PREBUILT_KERNEL := device/samsung/lt013g/prebuilt/kernel-7.3.0-pjarczak-G1130-VERIFIED-zImage.bin" not in config:
        refuse("Android TARGET_PREBUILT_KERNEL not pinned to accepted G1130 zImage")
    if sha(zimage) != ZIMAGE_SHA or sha(dtb) != DTB_SHA or dtb[:4] != bytes.fromhex("d00dfeed"):
        refuse("G1130 kernel or appended DTB differs from known physical BOOT")
    rules = (DEVICE / "t311-custom-bootimg.mk").read_text()
    if ("T311_BOOT_VARIANT ?= g1130-verified" not in rules or
        "T311_BOOT_ABI_CHECK := device/samsung/lt013g/tools/g1139-verify-pinned-g1130-release.py" not in rules):
        refuse("G1130 Android custom BOOT recipe not installed")
    print("G1139_BOOT_PREBUILT_AND_TWRP_PASS " + BOOT_SHA)

if "--built" in options:
    check_file(BUILT, BOOT_SHA)
    print("G1139_ANDROID_OUTPUT_BOOT_PASS " + BOOT_SHA)
