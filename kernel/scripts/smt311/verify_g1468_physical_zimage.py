#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path

BOOT_SHA = "acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565"
KERNEL_SHA = "12f3feb6c87cc19e4d256136ef000da864d52d215354fe85fa38cf970acd0a0e"

def sha(data):
    return hashlib.sha256(data).hexdigest()

parser = argparse.ArgumentParser()
parser.add_argument("--accepted-ota", type=Path, required=True)
parser.add_argument("--built-zimage", type=Path, required=True)
parser.add_argument("--built-3g-dtb", type=Path, required=True)
parser.add_argument("--dtb-only", action="store_true")
args = parser.parse_args()
with zipfile.ZipFile(args.accepted_ota) as archive:
    if archive.testzip() is not None:
        raise SystemExit("G1468_INVALID_RELEASE_ZIP_CRC")
    accepted = archive.read("boot.img")
if len(accepted) != 8388608 or accepted[:8] != b"ANDROID!" or sha(accepted) != BOOT_SHA:
    raise SystemExit("G1468_INVALID_ACCEPTED_G1441_BOOT")
kernel_len = struct.unpack_from("<I", accepted, 8)[0]
page = struct.unpack_from("<I", accepted, 36)[0]
if page != 2048 or kernel_len <= 0 or kernel_len >= len(accepted) - page:
    raise SystemExit("G1468_INVALID_ACCEPTED_BOOT_LAYOUT")
payload = accepted[page:page + kernel_len]
if sha(payload) != KERNEL_SHA:
    raise SystemExit("G1468_PINNED_PHYSICAL_ZIMAGE_SHA_INVALID")
built_dtb = args.built_3g_dtb.read_bytes()
physical_dtb_sha = "6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb"
if sha(built_dtb) != physical_dtb_sha or len(built_dtb) != 86489:
    raise SystemExit("G1470_BUILT_3G_DTB_MISMATCH")
if not payload.endswith(built_dtb):
    raise SystemExit("G1470_ACCEPTED_BOOT_DTB_MISMATCH")
print("G1470_PHYSICAL_LT013G_3G_DTB_BIT_IDENTICAL_PASS")
print("G1470_DTB_SHA256=" + physical_dtb_sha)
if args.dtb_only:
    print("G1470_ONLY_DTB_PROVEN_NOT_ZIMAGE_NOT_FLASHED")
    raise SystemExit(0)
physical_zimage = payload[:-len(built_dtb)]
expected_zimage_sha = "4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6"
if sha(physical_zimage) != expected_zimage_sha or len(physical_zimage) != 6387040:
    raise SystemExit("G1471_PHYSICAL_ZIMAGE_SUBPAYLOAD_INVALID")
built = args.built_zimage.read_bytes()
if built == physical_zimage:
    print("G1468_PRODUCTION_KERNEL_ZIMAGE_BIT_EXACT_WITH_ACCEPTED_G1441_BOOT_PASS")
else:
    print("G1468_PRODUCTION_ZIMAGE_DIFFERS_FROM_ACCEPTED_BOOT_DO_NOT_CLAIM_BIT_REPRO")
print("BOOT_SHA256=" + sha(accepted))
print("EXPECTED_ZIMAGE_SHA256=" + expected_zimage_sha)
print("BUILT_ZIMAGE_SHA256=" + sha(built))
print("ACCEPTED_ZIMAGE_BYTES=" + str(len(physical_zimage)))
print("BUILT_ZIMAGE_BYTES=" + str(len(built)))
if built != physical_zimage:
    raise SystemExit(42)
