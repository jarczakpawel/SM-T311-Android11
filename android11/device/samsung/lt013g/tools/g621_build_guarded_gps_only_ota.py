#!/usr/bin/env python3
"""Produce signed, one-file GPS TWRP ZIP with exact live partition guards.

Only touches existing /system/system/vendor/lib/hw/gps.default.so.
Never writes BOOT, RECOVERY, modem, EFS, or any other SYSTEM file.
"""
from pathlib import Path
from hashlib import sha256
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED
import json
import subprocess
import tempfile

ANDROID = Path("/home/debian/android/lineage-18.1-sm-t311")
PORT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
BASE = PORT / "artifacts/G597-EXACT-KERNEL-RELEASE/G597-BOOT-ONLY-TWRP-SIGNED.zip"
BASE_SHA = "8542edf6c90deba04b3e171123c4d44b45e57f9614267a75598a5f71036899a1"
NEW_GPS = ANDROID / "out/target/product/lt013g/system/vendor/lib/hw/gps.default.so"
OLD = "2e326e700513cb86b79d6cd7ef77adb3d1b595c46e8b69916ea08ae5c7485cd9"
NEW = "102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2"
BOOT = "7022bffae070737ff3eec9da218f754d4d569f9b14821ce8fb3ebfa967dc1e28"
RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
SIZE_SYSTEM = "2235564032"
OUT = PORT / "artifacts/G621-GPS-NMEA-ONEFILE-OTA"
FILENAME = OUT / "G621-G620-STRICT-NMEA-GPS-ONLY-TWRP-SIGNED.zip"

def digest(raw): return sha256(raw).hexdigest()

def main():
    if digest(BASE.read_bytes()) != BASE_SHA: raise ValueError("TWRP update-binary reference changed")
    blob = NEW_GPS.read_bytes()
    if digest(blob) != NEW: raise ValueError("GPS HAL changed since G620 validation")
    if not blob.startswith(bytes((0x7f, 0x45, 0x4c, 0x46))):
        raise ValueError("payload is not ELF")
    if FILENAME.exists(): raise FileExistsError("refusing to overwrite a signed release ZIP")
    guard = """#!/sbin/sh
set -eu
bb=/sbin/busybox
fail() { echo "G621 REFUSED: $1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }
gps=/system/system/vendor/lib/hw/gps.default.so
boot=/dev/block/mmcblk0p9
rec=/dev/block/mmcblk0p10
system=/dev/block/mmcblk0p20
[ -b "$boot" ] && [ -b "$rec" ] && [ -b "$system" ] || fail "missing partition"
[ "$("$bb" blockdev --getsize64 "$system")" = "__SIZE__" ] || fail "SYSTEM geometry"
[ "$(hash "$boot")" = "__BOOT__" ] || fail "unexpected installed BOOT"
[ "$(hash "$rec")" = "__REC__" ] || fail "unexpected TWRP RECOVERY"
[ -f "$gps" ] || fail "GPS HAL file missing"
case "$1" in
pre) [ "$(hash "$gps")" = "__OLD__" ] || [ "$(hash "$gps")" = "__NEW__" ] || fail "unexpected live GPS HAL";;
post) [ "$(hash "$gps")" = "__NEW__" ] || fail "written GPS HAL hash mismatch";;
*) fail "invalid verification phase";;
esac
exit 0
""".replace("__SIZE__", SIZE_SYSTEM).replace("__BOOT__", BOOT).replace("__REC__", RECOVERY).replace("__OLD__", OLD).replace("__NEW__", NEW)
    updater = """ui_print("G621: guarded GNSS NMEA-only update, no BOOT/RECOVERY/modem changes");
ifelse(is_mounted("/system"), unmount("/system"), ui_print("Mounting G621 SYSTEM"));
assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));
assert(file_getprop("/system/system/build.prop", "ro.build.version.sdk") == "30");
assert(package_extract_file("META-INF/g621-gps-guard.sh","/tmp/g621-gps-guard.sh"));
assert(run_program("/sbin/sh","/tmp/g621-gps-guard.sh","pre") == "0");
assert(package_extract_file("payload/gps.default.so","/system/system/vendor/lib/hw/gps.default.so"));
set_metadata("/system/system/vendor/lib/hw/gps.default.so","uid",0,"gid",0,"mode",0644);
assert(run_program("/sbin/sh","/tmp/g621-gps-guard.sh","post") == "0");
unmount("/system");
ui_print("G621 GPS HAL SHA verified; kernel/system elsewhere untouched");
set_progress(1.000000);
"""
    with ZipFile(BASE) as z:
        if z.testzip() is not None: raise ValueError("bad reference zip")
        updater_binary = z.read("META-INF/com/google/android/update-binary")
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="g621-ota-") as t:
        unsigned = Path(t) / "unsigned.zip"
        with ZipFile(unsigned, "w", ZIP_DEFLATED) as z:
            z.writestr("META-INF/com/google/android/updater-script", updater)
            z.writestr("META-INF/g621-gps-guard.sh", guard)
            z.writestr("META-INF/com/google/android/update-binary", updater_binary, compress_type=ZIP_STORED)
            z.writestr("payload/gps.default.so", blob)
        subprocess.run(["java","-Xmx900m",
           "-Djava.library.path=" + str(ANDROID / "out/host/linux-x86/lib64"),
           "-jar",str(ANDROID/"out/host/linux-x86/framework/signapk.jar"),"-w",
           str(ANDROID/"build/make/target/product/security/testkey.x509.pem"),
           str(ANDROID/"build/make/target/product/security/testkey.pk8"),
           str(unsigned),str(FILENAME)],cwd=ANDROID,check=True)
    with ZipFile(FILENAME) as z:
        if z.testzip() is not None: raise ValueError("signed package CRC failure")
        assert z.read("payload/gps.default.so") == blob
        assert z.read("META-INF/g621-gps-guard.sh") == guard.encode()
        assert z.read("META-INF/com/google/android/updater-script") == updater.encode()
        assert all(name.startswith("META-INF/") or name=="payload/gps.default.so" for name in z.namelist())
    manifest = {
      "release": "G621",
      "status": "SIGNED_HOST_ONLY_NOT_INSTALLED",
      "system_write_paths": ["/system/system/vendor/lib/hw/gps.default.so"],
      "expected_current_gps": OLD,
      "gps_new": NEW,
      "expected_boot": BOOT,
      "expected_recovery": RECOVERY,
      "zip_sha256": digest(FILENAME.read_bytes()),
      "zip_bytes": FILENAME.stat().st_size,
    }
    (OUT/"G621_GPS_ONLY_MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\\n")
    print(json.dumps(manifest,indent=2))
if __name__ == "__main__":
    main()
