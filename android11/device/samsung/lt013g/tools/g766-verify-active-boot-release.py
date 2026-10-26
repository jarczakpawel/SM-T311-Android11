#!/usr/bin/env python3
"""G766: self-contained, read-only gate for physical G730 SM-T311 boot.

Never writes any image or tablet partition. Source image is committed to the
SM-T311 device repository; expected hashes are from TWRP physical readback.
"""
from pathlib import Path
import hashlib
import struct

ROOT = Path(__file__).resolve().parents[4]
DEVICE = ROOT / "device/samsung/lt013g"
BOOT = DEVICE / "prebuilt/BOOT-7.3.0-pjarczak-G730-OTG-EVAL-8MiB.img"
TWRP = DEVICE / "prebuilt/recovery-current.img"
KERNEL = DEVICE / "prebuilt/kernel-7.3.0-pjarczak-G730-EXACT-zImage.bin"
EXPECTED_KERNEL_SIZE = 6473257
EXPECTED_KERNEL_SHA = "0e5e95313a1b6aae2b54fcd28c6bfed38be6e1f58f4d3ba1ea4270679b6d302e"
EXPECTED = {
    BOOT: (8388608, "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"),
    TWRP: (8388608, "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"),
}
for image, (size, expected) in EXPECTED.items():
    if not image.is_file() or image.stat().st_size != size:
        raise SystemExit(f"G766 BLOCKED: missing/bad image geometry: {image}")
    hasher = hashlib.sha256()
    with image.open("rb") as file:
        first = file.read(4096)
        hasher.update(first)
        for data in iter(lambda: file.read(1048576), b""):
            hasher.update(data)
    if first[:8] != b"ANDROID!" or hasher.hexdigest() != expected:
        raise SystemExit(f"G766 BLOCKED: BOOT/TWRP header or image SHA mismatch: {image}")
    page_size = struct.unpack_from("<I", first, 36)[0]
    if page_size not in (2048, 4096):
        raise SystemExit(f"G766 BLOCKED: invalid boot image page size: {image}")
    print(f"G766_PASS {image.name}: {size} bytes sha256={expected}")
# G873 guards Android BoardConfig TARGET_PREBUILT_KERNEL independently of
# custom G730 full-image packaging. The kernel zImage must be byte-identical
# to the payload inside the same physically accepted G730 BOOT image.
if not KERNEL.is_file() or KERNEL.stat().st_size != EXPECTED_KERNEL_SIZE:
    raise SystemExit("G873 BLOCKED: missing or wrong extracted G730 zImage")
with BOOT.open("rb") as src:
    header = src.read(2048)
    length = struct.unpack_from("<I", header, 8)[0]
    page_size = struct.unpack_from("<I", header, 36)[0]
    if length != EXPECTED_KERNEL_SIZE or page_size != 2048:
        raise SystemExit("G873 BLOCKED: unexpected G730 BOOT kernel header")
    src.seek(page_size)
    embedded = src.read(length)
if len(embedded) != length or hashlib.sha256(embedded).hexdigest() != EXPECTED_KERNEL_SHA:
    raise SystemExit("G873 BLOCKED: BOOT embedded kernel SHA mismatch")
if KERNEL.read_bytes() != embedded:
    raise SystemExit("G873 BLOCKED: TARGET_PREBUILT_KERNEL does not match G730 BOOT")
print("G873 G730 EMBEDDED KERNEL EXACT zImage 6473257 BYTES SHA PASS")
print("G766 BOOT/RECOVERY RELEASE SOURCE PIN PASS; FLASH AUTHORIZATION: NONE")
