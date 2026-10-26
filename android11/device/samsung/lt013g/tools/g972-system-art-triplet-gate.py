#!/usr/bin/env python3
"""G972: fail-closed ART v021 host input consistency for three SM-T311 system apps.

The DEX location checksum in a VDEX file must match the ZIP CRC of the
corresponding classes.dex in its APK. This checks source-built files only;
stale /data/dalvik-cache OAT/VDEX must be handled by Android's targeted dexopt.
No device writes, cache wipes, reboot, modem or secret material.
"""
import struct
from pathlib import Path
from zipfile import ZipFile

ANDROID = Path(__file__).resolve().parents[4]
SYSTEM = ANDROID / "out/target/product/lt013g/system"
PACKAGES = (
    ("LineageParts", "priv-app/LineageParts"),
    ("Settings", "system_ext/priv-app/Settings"),
    ("SystemUI", "system_ext/priv-app/SystemUI"),
)

def main():
    for name, rel in PACKAGES:
        folder = SYSTEM / rel
        apk = folder / f"{name}.apk"
        vdex = folder / "oat/arm" / f"{name}.vdex"
        odex = folder / "oat/arm" / f"{name}.odex"
        if any((not f.is_file() or f.is_symlink()) for f in (apk, vdex, odex)):
            raise SystemExit(f"G972_BLOCKED: missing or unsafe APK/ODEX/VDEX: {name}")
        with ZipFile(apk) as z:
            dex_infos = [x for x in z.infolist()
                         if x.filename == "classes.dex"
                         or (x.filename.startswith("classes") and
                             x.filename[7:-4].isdigit() and x.filename.endswith(".dex"))]
        def dex_index(name):
            return 1 if name == "classes.dex" else int(name[7:-4])
        dex_infos.sort(key=lambda x: dex_index(x.filename))
        if not dex_infos or [dex_index(x.filename) for x in dex_infos] != list(range(1, len(dex_infos)+1)):
            raise SystemExit(f"G972_BLOCKED: invalid or sparse multidex ordering: {name}")
        data = vdex.read_bytes()
        if len(data) < 28 or data[:12] != b"vdex021\x00000\x00":
            raise SystemExit(f"G972_BLOCKED: unexpected ART VDEX header: {name}")
        count = struct.unpack_from("<I", data, 12)[0]
        if count != len(dex_infos) or len(data) < 28 + 4*count:
            raise SystemExit(f"G972_BLOCKED: dex count differs from VDEX: {name}")
        expected = [x.CRC for x in dex_infos]
        actual = list(struct.unpack_from(f"<{count}I", data, 28))
        if expected != actual:
            raise SystemExit(f"G972_BLOCKED: dex location checksums mismatch: {name}")
        hdr = odex.read_bytes()[:20]
        if len(hdr) < 20 or hdr[:5] != b"\x7fELF\x01" or struct.unpack_from("<H",hdr,18)[0] != 40:
            raise SystemExit(f"G972_BLOCKED: not an ARM32 odex: {name}")
        print(f"G972_ART_APK_VDEX_ODEX_PASS {name} dex={count} crc={','.join(map(str, expected))}")
    print("G972_ART_3_OF_3_HOST_PASS; runtime DATA cache separate; modem untouched")

if __name__ == "__main__":
    main()
