#!/usr/bin/env python3
from hashlib import sha1, sha256, file_digest
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED, ZIP_DEFLATED
import json
import struct
import subprocess
import tempfile

ROOT = Path("/home/debian/android/lineage-18.1-sm-t311")
PROJECT = Path("/home/debian/Projekty/samsung")
PORT = PROJECT / "linux-7.3-port"
PREVIOUS = PORT / "artifacts/G582-SAFE-KERNEL-XARRAY-PMU-DVFS-DIAG/BOOT-G582-LINUX73-PMU-XARRAY-DVFS-8MiB-HOST-ONLY.img"
ZIMAGE = PORT / "build/smt311-g513-config-v7.3/arch/arm/boot/zImage"
DTB = PORT / "build/smt311-g513-config-v7.3/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb"
UPDATER_SOURCE = PROJECT / "artifacts/SM-T311-G586-VERIFIED-SYSTEM-20261020/G587-G586-TWRP-GUARDED-SIGNED.zip"
RELEASE = "G596"
OUTPUT = PORT / "artifacts/G596-EXYNOS-DEVFREQ-ERROR-PROPAGATION"
OLD_BOOT = "f7c5091dcba5cbbf012d9eab5ac4a505834a813e272442d8ad8c00c24ae6ee65"
SYSTEM = "229e1a8cbf037fe68cfa8d190771f13c0207b46c0c31f874d0af656ebbf31ab1"
RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
RAMDISK = "681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd"
ZIMAGE_SHA = "b762fa0254e74b0dc0c9b824e484531a464b27c377b8e6436eb60498657f1fee"
DTB_SHA = "6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb"
SIGNED_BASE = "157e4fcce298e3fab7f54b9cf6087cb31060022e4f247a1e2b5b92201a4bac2a"

def digest(path):
    with path.open("rb") as handle:
        return file_digest(handle, "sha256").hexdigest()

def build():
    if digest(PREVIOUS) != OLD_BOOT or digest(ZIMAGE) != ZIMAGE_SHA or digest(DTB) != DTB_SHA:
        raise ValueError("kernel baseline or compiled payload changed")
    if digest(UPDATER_SOURCE) != SIGNED_BASE:
        raise ValueError("TWRP updater baseline changed")

    previous = PREVIOUS.read_bytes()
    if len(previous) != 8388608 or previous[:8] != b"ANDROID!":
        raise ValueError("BOOT header or length unexpected")
    kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size, second_addr, tags, page, dt_size, reserved = struct.unpack_from("<10I", previous, 8)
    if page != 2048 or second_size or dt_size or (kernel_addr, ramdisk_addr, tags) != (0x10008000, 0x12000000, 0x10000100):
        raise ValueError("BOOT format mismatch")
    pad = lambda n: (n + page - 1) // page * page
    ram_off = page + pad(kernel_size)
    ramdisk = previous[ram_off:ram_off + ramdisk_size]
    if sha256(ramdisk).hexdigest() != RAMDISK or any(previous[ram_off + pad(ramdisk_size):]):
        raise ValueError("BOOT ramdisk or trailer unexpected")

    kernel = ZIMAGE.read_bytes() + DTB.read_bytes()
    new_header = bytearray(previous[:page])
    struct.pack_into("<I", new_header, 8, len(kernel))
    ident = sha1()
    for part in (kernel, ramdisk, b""):
        ident.update(part)
        ident.update(struct.pack("<I", len(part)))
    new_header[576:596] = ident.digest()
    packed = bytes(new_header) + kernel + bytes(pad(len(kernel)) - len(kernel)) + ramdisk
    packed += bytes(pad(ramdisk_size) - ramdisk_size)
    if len(packed) > len(previous):
        raise ValueError("BOOT overflow")
    boot = packed + bytes(len(previous) - len(packed))
    new_hash = sha256(boot).hexdigest()

    script = """#!/sbin/sh
set -eu
bb=/sbin/busybox
boot=/dev/block/mmcblk0p9
recovery=/dev/block/mmcblk0p10
system=/dev/block/mmcblk0p20
fail() { echo "G596 BOOT UPDATE REFUSED: $1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }
[ -b "$boot" ] && [ -b "$system" ] && [ -b "$recovery" ] || fail "missing partition"
[ "$("$bb" blockdev --getsize64 "$boot")" = "8388608" ] || fail "BOOT geometry"
[ "$("$bb" blockdev --getsize64 "$system")" = "2235564032" ] || fail "SYSTEM geometry"
[ -L /dev/block/platform/dw_mmc/by-name/BOOT ] || fail "missing BOOT alias"
[ "$("$bb" readlink -f /dev/block/platform/dw_mmc/by-name/BOOT)" = "$boot" ] || fail "wrong BOOT alias"
[ "$(hash "$recovery")" = "__RECOVERY__" ] || fail "unexpected recovery"
case "$1" in
pre)
  [ "$(hash "$boot")" = "__OLD__" ] || fail "unexpected installed BOOT"
  [ "$(hash "$system")" = "__SYSTEM__" ] || fail "unexpected installed SYSTEM"
  ;;
post)
  [ "$(hash "$boot")" = "__NEW__" ] || fail "new BOOT hash mismatch"
  ;;
*) fail "invalid mode";;
esac
exit 0
""".replace("G596", RELEASE).replace("__RECOVERY__", RECOVERY).replace("__OLD__", OLD_BOOT).replace("__SYSTEM__", SYSTEM).replace("__NEW__", new_hash)
    script_name = f"META-INF/t311-{RELEASE.lower()}-boot-guard.sh"
    updater = """assert(getprop("ro.product.device") == "lt013g" ||
    getprop("ro.product.device") == "lt013gxx" ||
    file_getprop("/default.prop", "ro.product.device") == "lt013g" ||
    abort("G596 blocked: not SM-T311 lt013g"));
ui_print("G596: Exynos devfreq event error propagation, BOOT only");
assert(package_extract_file("__GUARD__", "/tmp/t311-g596-boot-guard.sh"));
assert(run_program("/sbin/sh", "/tmp/t311-g596-boot-guard.sh", "pre") == "0");
assert(package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT"));
assert(run_program("/sbin/sh", "/tmp/t311-g596-boot-guard.sh", "post") == "0");
ui_print("G596 BOOT verified");
set_progress(1.000000);
""".replace("G596", RELEASE).replace("__GUARD__", script_name)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    image_path = OUTPUT / f"{RELEASE}-BOOT-8MiB.img"
    zip_path = OUTPUT / f"{RELEASE}-BOOT-ONLY-TWRP-SIGNED.zip"
    manifest_path = OUTPUT / f"{RELEASE}_RELEASE_MANIFEST.json"
    if any(p.exists() for p in (image_path, zip_path, manifest_path)):
        raise FileExistsError("refusing to overwrite existing release")
    image_path.write_bytes(boot)
    with ZipFile(UPDATER_SOURCE) as archive:
        update_binary = archive.read("META-INF/com/google/android/update-binary")
    with tempfile.TemporaryDirectory(prefix="g596-ota-") as tmp:
        unsigned = Path(tmp) / "unsigned.zip"
        with ZipFile(unsigned, "w", ZIP_DEFLATED) as archive:
            archive.writestr("META-INF/com/google/android/updater-script", updater)
            archive.writestr(script_name, script)
            archive.writestr("META-INF/com/google/android/update-binary", update_binary, compress_type=ZIP_STORED)
            archive.write(image_path, "boot.img", compress_type=ZIP_STORED)
        subprocess.run([
            "java", "-Xmx900m",
            "-Djava.library.path=" + str(ROOT / "out/host/linux-x86/lib64"),
            "-jar", str(ROOT / "out/host/linux-x86/framework/signapk.jar"),
            "-w",
            str(ROOT / "build/make/target/product/security/testkey.x509.pem"),
            str(ROOT / "build/make/target/product/security/testkey.pk8"),
            str(unsigned), str(zip_path)
        ], check=True, cwd=ROOT)
    with ZipFile(zip_path) as archive:
        if archive.testzip() is not None or archive.read("boot.img") != boot:
            raise ValueError("signed BOOT payload corrupt")
        if archive.read("META-INF/com/google/android/updater-script").decode() != updater:
            raise ValueError("updater changed")
        if archive.read(script_name).decode() != script:
            raise ValueError("guard changed")
        if any(not item.startswith("META-INF/") and item != "boot.img" for item in archive.namelist()):
            raise ValueError("unexpected partition payload")
    result = {
        "release": RELEASE,
        "status": "HOST_SIGNED_NOT_FLASHED",
        "source_kernel_commit": "08e4784ad",
        "zimage_sha256": ZIMAGE_SHA,
        "dtb_sha256": DTB_SHA,
        "old_boot_sha256": OLD_BOOT,
        "new_boot_sha256": new_hash,
        "required_system_sha256": SYSTEM,
        "required_recovery_sha256": RECOVERY,
        "signed_zip_sha256": digest(zip_path),
        "signed_zip_size": zip_path.stat().st_size
    }
    manifest_path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    build()
