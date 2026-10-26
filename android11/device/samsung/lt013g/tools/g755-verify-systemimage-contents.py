#!/usr/bin/env python3
"""Read-only G755 release gate: inspect a rebuilt sparse system.img as ext4.

Extracts sparse image to a temporary raw image, hashes selected files using
debugfs (without mounting/flash), and deletes raw staging on completion.
Pins 10 unchanged reference files and six audited G962 Android build outputs; each image path also requires matching DAC, SELinux xattr and ext4 geometry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve().parents[4]
PRODUCT = SOURCE / "out/target/product/lt013g"
PROJECT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
SYSTEM_PARTITION_BYTES = 2235564032
EXPECTED = {
    "/system/bin/wificond": (
        "5606d985a8a37384e6830fd763497115a6d8b32a69f56d1aa34ac320f0ed82b4", "0755", 0, 2000, "u:object_r:wificond_exec:s0"),
    "/system/priv-app/MediaProvider/MediaProvider.apk": (
        "de4aa973de7787e11bbc99b9bfc4e3934d7d26b14ce605b470657cc21ca862f1", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/system_ext/priv-app/SystemUI/SystemUI.apk": (
        "dfc2d99b790791d86893818924344c138ce397eaf44b2014bf9eca8aaa7ac31c", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/priv-app/MediaProvider/oat/arm/MediaProvider.odex": (
        "e5b23ad85fb1b0fe60c7c933acec5384685c721d1ed4738721c50454aa10252e", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/priv-app/MediaProvider/oat/arm/MediaProvider.vdex": (
        "ba6e95940cc7fff72d6ad07af5ed837e1c5c048b090e6a2436372777dce21eee", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex": (
        "f1bf8b37413cd5774b9a226064d3dc60702c855bc152ceada613c36ef3871416", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex": (
        "d8ab9900ac62253c16ecab78d10a7b75341d05328f0fa7b0dda1eb0012e78720", "0644", 0, 0, "u:object_r:system_file:s0"),
    "/system/lib/libcodec2_hidl@1.0.so": (
        "77aab570ea9ceddc4d0f7fa0e50852bb8dd5c067e965ec090d188c01eaf21510", "0644", 0, 0, "u:object_r:system_lib_file:s0"),
    "/system/lib/libcameraservice.so": (
        "276c70c85b2665659b609871bca22f399ac6223d7366d0fc12090c4ef1729538", "0644", 0, 0, "u:object_r:system_lib_file:s0"),
    "/system/bin/vold": (
        "be0a2c91e4fb9e7dd19e00a2e2db45fa830b5971ceb84eb3dc48129b3491eeb5", "0755", 0, 2000, "u:object_r:vold_exec:s0"),
    "/system/bin/fsck.exfat": (
        "df8c2aebd54de8c83f90248f3acc0b37dce0b5536bf8c626e4cb3184a2ae7cd7", "0755", 0, 2000, "u:object_r:fsck_exec:s0"),
    "/system/bin/mkfs.exfat": (
        "a3ec09ad6e087d1078ec4dce002fd2711c0ca7822b6dacf92170cc1b8a63ff5e", "0755", 0, 2000, "u:object_r:mkfs_exec:s0"),
    "/system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service": (
        "fe0cc1bb9ba1caed0603f4cc8175d593af20b1cd585ea69ffaaffb785d9fa075", "0755", 0, 2000, "u:object_r:hal_usb_gadget_default_exec:s0"),
    "/system/vendor/lib/hw/gps.default.so": (
        "c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8", "0644", 0, 0, "u:object_r:vendor_file:s0"),
    "/sepolicy": (
        "4ee1dde32f6ea827e858af031080c8b39ac2f42ffd65665a4c9e835fbe75e586", "0644", 0, 0, "u:object_r:sepolicy_file:s0"),
    "/system/vendor/etc/selinux/vendor_sepolicy.cil": (
        "445ea6e25f3d65796471cdc248b7a423f94dae3ebc882ff7f8c8f5de439229d8", "0644", 0, 0, "u:object_r:vendor_configs_file:s0"),
}
# In system-as-root images, build output /system tree corresponds to filesystem
# /system. Most Android 11 system images include /system at filesystem root.
ROOTS = ("", "/system")

def run(*args, check=True):
    return subprocess.run(list(map(str, args)), capture_output=True, check=check)

def debugfs(image, op, path):
    return run("/sbin/debugfs", "-R", f"{op} {path}", image).stdout

def verify(image, roots):
    for root in roots:
        entries = {}
        for path, (expect, mode, uid, gid, label) in EXPECTED.items():
            actual_path = root + path
            result = run("/sbin/debugfs", "-R", "cat " + actual_path, image)
            if result.stderr and b"File not found by ext2_lookup" in result.stderr:
                break
            got = hashlib.sha256(result.stdout).hexdigest()
            if got != expect:
                break
            result_stat = run("/sbin/debugfs", "-R", "stat " + actual_path, image)
            stat = result_stat.stdout.decode(errors="replace")
            # Strip filesystem-specific times/ACL; check DAC and SELinux inode xattr.
            m = re.search(r"Mode:\s+0*(\d+)\s+Flags:", stat)
            u = re.search(r"User:\s+(\d+)\s+Group:\s+(\d+)", stat)
            if not m or not u or int(m.group(1), 8) != int(mode, 8) or (int(u.group(1)), int(u.group(2))) != (uid, gid):
                break
            result_xattr = run("/sbin/debugfs", "-R", "ea_get " + actual_path + " security.selinux", image)
            expected_xattr = ('"' + label + r'\000"').encode()
            if expected_xattr not in result_xattr.stdout:
                break
            entries[path] = {"sha256": got, "mode": mode, "uid": uid, "gid": gid, "selinux": label}
        if len(entries) == len(EXPECTED):
            return root, entries
    raise RuntimeError("System image does not contain the exact G962 release-locked files with matching SHA/DAC/SELinux in a supported root layout")

def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("--image", type=Path, default=PRODUCT / "system.img")
    cli.add_argument("--out", type=Path, default=PROJECT / "artifacts/G755-SYSTEMIMAGE-QA-20261021/IMAGE_QA.json")
    args = cli.parse_args()
    image = args.image.resolve()
    if not image.is_file() or image.stat().st_size < 1024 * 1024:
        sys.exit("MISSING/INVALID system image")
    sparse_tool = SOURCE / "out/host/linux-x86/bin/simg2img"
    if not sparse_tool.is_file():
        sys.exit("MISSING simg2img")
    if not Path("/sbin/debugfs").is_file():
        sys.exit("MISSING debugfs")
    if not Path("/usr/sbin/dumpe2fs").is_file():
        sys.exit("MISSING dumpe2fs")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="g755-image-audit-", dir=str(args.out.parent)) as temp:
        raw = Path(temp) / "system.raw.ext4"
        run(sparse_tool, image, raw)
        if raw.stat().st_size != SYSTEM_PARTITION_BYTES:
            raise RuntimeError("System image raw size differs from SM-T311 SYSTEM partition")
        superblock = run("/usr/sbin/dumpe2fs", "-h", raw).stdout.decode()
        count = re.search(r"(?m)^Block count:\s+(\d+)$", superblock)
        size = re.search(r"(?m)^Block size:\s+(\d+)$", superblock)
        state = re.search(r"(?m)^Filesystem state:\s+(\S+)$", superblock)
        features = re.search(r"(?m)^Filesystem features:\s+(.+)$", superblock)
        if not count or not size or not state or not features:
            raise RuntimeError("Unrecognized ext4 superblock")
        if int(count.group(1)) * int(size.group(1)) != SYSTEM_PARTITION_BYTES:
            raise RuntimeError("Ext4 geometry differs from SM-T311 SYSTEM partition")
        if state.group(1) != "clean" or "extent" not in features.group(1).split():
            raise RuntimeError("SYSTEM image filesystem is not a clean ext4 image")
        root, entries = verify(raw, ROOTS)
    # Write a report only after every check succeeds.
    image_hasher = hashlib.sha256()
    with image.open("rb") as src:
        for chunk in iter(lambda: src.read(4 * 1024 * 1024), b""):
            image_hasher.update(chunk)
    report = {
        "result": "PASS",
        "source": str(image),
        "image_sha256": image_hasher.hexdigest(),
        "system_partition_bytes": SYSTEM_PARTITION_BYTES,
        "filesystem_clean": True,
        "root_layout": root,
        "verified_file_count": len(entries),
        "files": entries,
    }
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n")
    print(json.dumps({"result":"PASS","root_layout":root,"files":len(entries),"report":str(args.out)},indent=2))

if __name__ == "__main__":
    main()
