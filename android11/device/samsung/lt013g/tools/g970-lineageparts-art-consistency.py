#!/usr/bin/env python3
"""G970: verify source-built LineageParts APK/ODEX/VDEX match ART v021.

Host-only; checks package archive location CRC against each VDEX location
checksum. This guards system builds, NOT /data ART caches on an already
running tablet. Never removes data, APKs, cache files or private OEM files.
"""
from pathlib import Path
import struct
from zipfile import ZipFile, BadZipFile

ROOT = Path(__file__).resolve().parents[4]
PACKAGE = ROOT / "out/target/product/lt013g/system/priv-app/LineageParts"
APK = PACKAGE / "LineageParts.apk"
VDEX = PACKAGE / "oat/arm/LineageParts.vdex"
ODEX = PACKAGE / "oat/arm/LineageParts.odex"

def check():
    for p in (APK, VDEX, ODEX):
        if not p.is_file() or p.is_symlink():
            raise RuntimeError("G970_BLOCKED missing or unsafe artifact: " + str(p))
    with ZipFile(APK) as apk:
        dex = [item for item in apk.infolist()
               if item.filename.startswith("classes") and item.filename.endswith(".dex")]
        # ART multidex ordering is classes.dex, classes2.dex ...
        dex.sort(key=lambda i: (i.filename != "classes.dex", i.filename))
        if len(dex) != 1 or dex[0].filename != "classes.dex":
            raise RuntimeError("G970_BLOCKED: expected one LineageParts classes.dex")
        archive_crc = dex[0].CRC
    data = VDEX.read_bytes()
    if len(data) < 32 or data[:12] != b"vdex021\x00000\x00":
        raise RuntimeError("G970_BLOCKED: not expected ART 11 VDEX v021")
    dex_count, size, bcp, clc = struct.unpack_from("<4I", data, 12)
    if dex_count != 1:
        raise RuntimeError("G970_BLOCKED: VDEX dex count mismatched")
    vdex_location_crc = struct.unpack_from("<I", data, 28)[0]
    if vdex_location_crc != archive_crc:
        raise RuntimeError(
            f"G970_BLOCKED: LineageParts.apk zip CRC {archive_crc} "
            f"!= VDEX location CRC {vdex_location_crc}")
    elf = ODEX.read_bytes()[:20]
    if elf[:5] != b"\x7fELF\x01" or struct.unpack_from("<H", elf, 18)[0] != 40:
        raise RuntimeError("G970_BLOCKED: ODEX not ARM32 ELF")
    print(f"G970_ART_SOURCE_MATCH_PASS: APK classes.dex CRC={archive_crc}, "
          "VDEX v021 matching, ODEX ARM32, host-only; live stale cache separate")

if __name__ == "__main__":
    check()
