#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

args = set(sys.argv[1:])
if not args or args - {"--stage", "--source", "--boot", "--built"}:
    sys.exit("Usage: g960-source-integrity.py --stage|--source|--boot|--built")

device = Path(__file__).resolve().parents[1]
android = device.parents[2]
project = Path(os.environ.get("SMT311_PROJECT_ROOT", "/home/debian/Projekty/samsung")).resolve()
lock = json.loads((device / "tools/g960-source-lock.json").read_text())
source = (project / lock["kernel_project_path"]).resolve()
android_kernel = android / "kernel/samsung/lt013g"
stage = device / "prebuilt/private-active-g958"
boot = stage / "boot.img"
kernel = stage / "zImage"

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            h.update(block)
    return h.hexdigest()

def git(*arguments):
    return subprocess.run(["git", *map(str, arguments)], capture_output=True, text=True)

if "--stage" in args:
    if not source.is_dir() or git("-C", source, "merge-base", "--is-ancestor", lock["kernel_source_git"], "HEAD").returncode:
        sys.exit("G960 BLOCKED: G958 source missing or replaced")
    stage.mkdir(parents=True, exist_ok=True)
    for relative, dest, expected in (
        (lock["private_boot_archive"], boot, lock["boot_sha256"]),
        (lock["kernel_archive_zimage"], kernel, lock["kernel_zimage_sha256"]),
    ):
        original = project / relative
        if not original.is_file() or digest(original) != expected:
            sys.exit(f"G960 BLOCKED: private staged source absent or changed: {relative}")
        if not dest.is_file() or digest(dest) != expected:
            temporary = dest.with_suffix(dest.suffix + ".tmp")
            shutil.copyfile(original, temporary)
            os.replace(temporary, dest)
    if android_kernel.is_symlink() and (android_kernel.resolve() != source or os.readlink(android_kernel) != str(source)):
        temporary = android_kernel.with_name(android_kernel.name + ".g960")
        temporary.unlink(missing_ok=True)
        temporary.symlink_to(source, target_is_directory=True)
        os.replace(temporary, android_kernel)
    if not android_kernel.is_symlink() or (android_kernel.resolve() != source or os.readlink(android_kernel) != str(source)):
        sys.exit("G960 BLOCKED: kernel source path is not a replaceable symlink")
    print("G960_STAGE_PASS: pinned private G958 BOOT/kernel and canonical source link; no flash")
    args.update({"--source", "--boot"})

if "--source" in args:
    if not android_kernel.is_symlink() or (android_kernel.resolve() != source or os.readlink(android_kernel) != str(source)):
        sys.exit("G960 BLOCKED: Android kernel source is not pinned G958 worktree")
    board_common = (android / "device/samsung/lt01-common/BoardConfigCommon.mk").read_text()
    if "BOARD_PROVIDES_LIBRIL := true" not in board_common:
        sys.exit("G960 BLOCKED: Samsung XMM6262 libril provider disabled")
    for rel, commit in [("kernel/samsung/lt013g", lock["kernel_source_git"]), *lock["repositories_min_ancestry"].items()]:
        directory = android / rel
        if git("-C", directory, "merge-base", "--is-ancestor", commit, "HEAD").returncode:
            sys.exit(f"G960 BLOCKED: missing committed source history: {rel}: {commit}")
        if rel in lock["strict_exact_repositories"]:
            head = git("-C", directory, "rev-parse", "HEAD")
            if head.returncode or head.stdout.strip() != commit:
                sys.exit(f"G960 BLOCKED: version changed without lock update: {rel}")
        if git("-C", directory, "diff", "--quiet", "HEAD").returncode:
            sys.exit(f"G960 BLOCKED: uncommitted tracked source changes: {rel}")
        result = git("-C", directory, "rev-parse", "--short", "HEAD")
        if result.returncode:
            sys.exit(f"G960 BLOCKED: git unavailable: {rel}")
        print(f"G960_SOURCE_PASS {rel} {result.stdout.strip()}")

if "--boot" in args:
    for path, expected, size in [
        (boot, lock["boot_sha256"], lock["boot_bytes"]),
        (kernel, lock["kernel_zimage_sha256"], lock["kernel_zimage_bytes"]),
        (device / "prebuilt/recovery-current.img", lock["recovery_sha256"], 8388608),
    ]:
        if not path.is_file() or path.stat().st_size != size or digest(path) != expected:
            sys.exit(f"G960 BLOCKED: pinned BOOT/RECOVERY input missing or changed: {path}")
    with boot.open("rb") as source_boot:
        header = source_boot.read(2048)
        if header[:8] != b"ANDROID!" or struct.unpack_from("<I", header, 36)[0] != 2048:
            sys.exit("G960 BLOCKED: invalid Android BOOT header")
        length = struct.unpack_from("<I", header, 8)[0]
        embedded = source_boot.read(length)
        zimage = kernel.read_bytes()
        dtb = embedded[len(zimage):]
        if length != len(zimage) + lock["kernel_appended_dtb_bytes"] or not embedded.startswith(zimage):
            sys.exit("G960 BLOCKED: embedded zImage geometry or data differs from G958")
        if dtb[:4] != bytes.fromhex("d00dfeed") or hashlib.sha256(dtb).hexdigest() != lock["kernel_appended_dtb_sha256"]:
            sys.exit("G960 BLOCKED: appended SM-T311 DTB changed")
    config = (device / "BoardConfig.mk").read_text()
    rules = (device / "t311-custom-bootimg.mk").read_text()
    if "TARGET_PREBUILT_KERNEL := device/samsung/lt013g/prebuilt/private-active-g958/zImage" not in config:
        sys.exit("G960 BLOCKED: BoardConfig targets old kernel")
    if "T311_BOOT_VARIANT ?= g958-current" not in rules:
        sys.exit("G960 BLOCKED: custom BOOT defaults to old version")
    print(f"G960_BOOT_PASS active G958 {lock['boot_sha256']}; TWRP unchanged; no flash")

if "--built" in args:
    expected_markers = [
        (android / lock["codec2_module"],
         (b"G982_DETACHED_MFC_OUTPUT_COPY" if
          lock.get("g982_detached_output_diagnostic", {}).get("acceptance", "") ==
          "PENDING host compile and long strict real-device QA" else
          b"G967_DMABUF_SYNC_START_FAIL"),
         b"bd0a0b652c2835025121714b2683a78728d992ae9615ecb63c8037453b56de87"),
        (android / lock["ril_module"], b"G835 operator SUCCESS with incomplete payload", b"G835 OPERATOR SUCCESS malformed payload"),
    ]
    for path, required, forbidden in expected_markers:
        if not path.is_file():
            sys.exit(f"G960 BLOCKED: missing compiled module: {path}")
        binary = path.read_bytes()
        if binary[:5] != b"\x7fELF\x01" or struct.unpack_from("<H", binary, 18)[0] != 40:
            sys.exit(f"G960 BLOCKED: not ARM32 ELF: {path}")
        if required not in binary or (len(forbidden) < 60 and forbidden in binary):
            sys.exit(f"G960 BLOCKED: stale or wrong compiled module: {path.name}")
        if path == android / lock["codec2_module"] and digest(path) == lock.get("old_codec2_sha256"):
            sys.exit("G960 BLOCKED: old G886 Codec2 still in build output")
        print(f"G960_BUILT_PASS {path.name} sha256={digest(path)}")
    systemui = android / lock["systemui_module"]
    if not systemui.is_file() or digest(systemui) == lock["old_systemui_sha256"]:
        sys.exit("G960 BLOCKED: G908 SystemUI absent or stale")
    with zipfile.ZipFile(systemui) as package:
        if not any(
            "classes" in name and name.endswith(".dex")
            and b"G908_RECORDING_ROTATION_FIXED=" in package.read(name)
            and b"G908_RECORDING_ROTATION_RESTORED" in package.read(name)
            for name in package.namelist()
        ):
            sys.exit("G960 BLOCKED: SystemUI APK is missing G908 bytecode")
    print(f"G960_BUILT_PASS SystemUI.apk G908 sha256={digest(systemui)}")
    for rel, expected in lock["compiled_module_sha256"].items():
        path = android / rel
        if not path.is_file() or digest(path) != expected:
            sys.exit(f"G960 BLOCKED: compiled artifact differs from pinned release: {rel}")
    if lock.get("g982_detached_output_diagnostic", {}).get("acceptance", "") ==             "PENDING host compile and long strict real-device QA":
        print("G960_G982_DIAGNOSTIC_BUILT_SHA_PASS_NO_FLASH: encoder unqualified")
    else:
        print("G960_BUILT_ALL_FOUR_SHA_PASS: H264 release, Samsung RIL G835, SystemUI G908, USB G812")
