#!/usr/bin/env python3
import argparse
import hashlib
import io
import json
import struct
import sys
import zipfile
from pathlib import Path

EXPECTED = {
    # Must remain byte-identical to the current physical mmcblk0p10 TWRP.
    # Do not silently replace user recovery while flashing a SYSTEM update.
    "IMAGES/recovery.img": "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f",
    "BOOT/kernel": "d55e8091dc9a26bd7e94ff49254b308c823f2df00d9cb1d7f8befc34c9e0770a",
    "SYSTEM/vendor/lib/hw/hwcomposer.drm_minigbm.so": "03053283ff7f9d324b969bb6a19e2806131f8ee5f883b795220481c0a8762cbb",
    "SYSTEM/lib/libEGL.so": "9d2472806965e9d521c32fc2ff6389aab947fdf9fe9c3034c442dcf71e8d7394",
    "SYSTEM/lib/libnativedisplay.so": "39a22cd8ac6085f670d8651ec61460e811cf00f1e75c9a1464833d858b89d19c",
    "SYSTEM/vendor/etc/media_codecs.xml": "d0bbf0a71f8e57f6afb7fce6d8ef7d7a1da334f99c26f16d8206d0eea9f1539e",
    "SYSTEM/vendor/etc/media_profiles_V1_0.xml": "a5f374566b5e43a28711e2ff595c4e10a1742f38ed703b8104f947d86670d46b",
    "SYSTEM/apex/com.android.mediaprovider/priv-app/MediaProvider/MediaProvider.apk": "39d704cc73f3b50bd7385256a9694f9b3e3009c8cf26da314cb98a16a614fb54",
    # G288: legacy SM-T311 requires a registered MediaProvider priv-app.
    "SYSTEM/priv-app/MediaProvider/MediaProvider.apk": "9b2497f6e57a74aac135db03eed5c39cd591d6bfccfc97fa6528c0cb33ab1d59",
    # G278: Lineage SDK recompilation embeds the lt013g HIDL opt-out into
    # Trebuchet's classes2.dex. G241 ShelfScrimView bytecode is identical
    # after normalizing DEX index/offset relocation (877 lines, zero diff);
    # resources and manifest are unchanged. Keep exact APK pinning.
    "SYSTEM/system_ext/priv-app/TrebuchetQuickStep/TrebuchetQuickStep.apk": "4474de4fef17e32d7212996363d7052a4fca2a459a173b9b7362b7e2300aae3e",
}
FORBIDDEN = (
    "SYSTEM/priv-app/AudioFX/",
    "SYSTEM/app/AudioFX/",
    # No abandoned updater APK, oat or vdex in our offline SM-T311 ROM.
    "SYSTEM/priv-app/Updater/",
    "SYSTEM/etc/permissions/org.lineageos.trust.xml",
)
NEEDED = (
    "SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil",
    "SYSTEM/vendor/etc/selinux/vendor_file_contexts",
    "SYSTEM/etc/selinux/plat_sepolicy.cil",
    "SYSTEM/etc/sysconfig/lt013g-mediaprovider-hiddenapi.xml",
)
SEPOLICY_TOKENS = (b"hal_audio_default_tmpfs", b"lt013g_efs_block_device")
# Use the exact JVM class descriptor, not the word TrustInterfaceService:
# the retained public TrustInterface SDK contains a benign exception string
# mentioning that name even though the optional daemon class is absent.
TRUST_STRINGS = (b"Lorg/lineageos/platform/internal/TrustInterfaceService;",
                 b"TrustOnBoardingActivity", b"TrustPreferences")
# Validated against SM-T311 BoardConfigCommon.mk and the physical eMMC p20.
PARTITION_SIZES = {
    "boot": 8388608,
    "recovery": 8388608,
    "system": 2235564032,
}
SPARSE_MAGIC = 0xED26FF3A


def check_partition_bounds(z, names, issues, results):
    meta_name = "META/misc_info.txt"
    if meta_name not in names:
        issues.append("MISSING_META_MISC_INFO")
        return
    fields = dict(line.split("=", 1) for line in
                  z.read(meta_name).decode("utf-8").splitlines() if "=" in line)
    for partition, limit in PARTITION_SIZES.items():
        reported = fields.get(f"{partition}_size")
        if reported is None or reported != str(limit):
            issues.append(f"PARTITION_CONFIG_MISMATCH {partition} expected={limit} got={reported}")
        path = f"IMAGES/{partition}.img"
        if path not in names:
            issues.append(f"MISSING_PARTITION_IMAGE {partition}")
            continue
        stored_bytes = z.getinfo(path).file_size
        logical_bytes = stored_bytes
        sparse = False
        with z.open(path) as f:
            header = f.read(28)
        if len(header) >= 28 and struct.unpack_from("<I", header)[0] == SPARSE_MAGIC:
            magic, major, minor, file_header, chunk_header, block_size, blocks, chunks, checksum = struct.unpack_from(
                "<IHHHHIIII", header)
            sparse = True
            if major != 1 or file_header < 28 or chunk_header < 12 or block_size not in (4096, 8192, 16384):
                issues.append(f"INVALID_SPARSE_IMAGE_HEADER {partition}")
            logical_bytes = block_size * blocks
        results[f"{partition}_image"] = {
            "stored_bytes": stored_bytes,
            "logical_bytes": logical_bytes,
            "physical_partition_bytes": limit,
            "sparse": sparse,
        }
        if logical_bytes > limit:
            issues.append(f"OVERSIZED_PARTITION_IMAGE {partition} logical={logical_bytes} physical={limit}")



def sha(stream):
    digest = hashlib.sha256()
    while data := stream.read(1024 * 1024):
        digest.update(data)
    return digest.hexdigest()


def verify(path):
    results = {}
    issues = []
    with zipfile.ZipFile(path) as z:
        names = set(z.namelist())
        check_partition_bounds(z, names, issues, results)
        for name, expected in EXPECTED.items():
            if name not in names:
                issues.append(f"MISSING {name}")
                continue
            with z.open(name) as f:
                actual = sha(f)
            results[name] = actual
            if actual != expected:
                issues.append(f"SHA_MISMATCH {name} expected={expected} got={actual}")
        for forbidden in FORBIDDEN:
            if any(n == forbidden or n.startswith(forbidden) for n in names):
                issues.append(f"FORBIDDEN {forbidden}")
        for mandatory in NEEDED:
            if mandatory not in names:
                issues.append(f"MISSING {mandatory}")
        whitelist_path = "SYSTEM/etc/sysconfig/lt013g-mediaprovider-hiddenapi.xml"
        if whitelist_path in names:
            expected = b'<hidden-api-whitelisted-app package="com.android.providers.media.module" />'
            if expected not in z.read(whitelist_path):
                issues.append("MEDIA_PROVIDER_PRIVATE_API_WHITELIST_MISMATCH")
        if "IMAGES/boot.img" in names and "BOOT/kernel" in names:
            boot = z.read("IMAGES/boot.img")
            if boot[:8] != b"ANDROID!":
                issues.append("INVALID_BOOT_MAGIC")
            else:
                size = struct.unpack_from("<I", boot, 8)[0]
                page = struct.unpack_from("<I", boot, 36)[0]
                with z.open("BOOT/kernel") as f:
                    kernel = f.read()
                if page < 512 or boot[page:page + size] != kernel:
                    issues.append("BOOT_IMAGE_KERNEL_DIFFERS_FROM_BOOT_KERNEL")
                results["boot_payload_kernel_sha256"] = hashlib.sha256(kernel).hexdigest()
                results["boot_image_sha256"] = hashlib.sha256(boot).hexdigest()
        else:
            issues.append("BOOT_IMAGE_OR_KERNEL_MISSING")
        contexts = "SYSTEM/vendor/etc/selinux/vendor_file_contexts"
        if contexts in names:
            text = z.read(contexts)
            if b"/dev/block/mmcblk0p3" not in text or b"lt013g_efs_block_device" not in text:
                issues.append("MISSING_EFS_CONTEXT")
        vendor_cil = "SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil"
        if vendor_cil in names:
            content = z.read(vendor_cil)
            for token in SEPOLICY_TOKENS:
                if token not in content:
                    issues.append(f"MISSING_VENDOR_SEPOLICY_TYPE {token.decode()}")
        for name in ("SYSTEM/framework/org.lineageos.platform.jar",
                     "SYSTEM/priv-app/LineageParts/LineageParts.apk"):
            if name not in names:
                issues.append(f"MISSING {name}")
                continue
            with zipfile.ZipFile(io.BytesIO(z.read(name))) as classes:
                class_bytes = b"".join(classes.read(n) for n in classes.namelist()
                                       if n.startswith("classes") and n.endswith(".dex"))
                for token in TRUST_STRINGS:
                    if token in class_bytes:
                        issues.append(f"TRUST_RUNTIME_CLASS_PRESENT {name} {token.decode()}")
        results["target_files_sha256"] = sha(open(path, "rb"))
        results["target_files_bytes"] = path.stat().st_size
    return {"device": "SM-T311", "target_files": str(path), "status": "PASS" if not issues else "FAIL",
            "issues": issues, "sha256": results}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("target_files", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = verify(args.target_files)
    text = json.dumps(result, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n")
    sys.exit(0 if result["status"] == "PASS" else 1)
