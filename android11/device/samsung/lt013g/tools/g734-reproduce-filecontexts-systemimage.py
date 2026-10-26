#!/usr/bin/env python3
"""G734: reproduce Android 11 file_contexts.bin and verify built SM-T311 ext4.

Source-only reproducible HOST procedure; never connects to tablet, runs
recovery, writes kernel partitions, installs OTA or changes SELinux mode.
Run --create-labels before 'm systemimage-nodeps' in a sparse build tree.
Run --verify-image afterward to inspect the sparse ext4 contents read-only.
"""
from __future__ import annotations
import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path

A = Path(__file__).resolve().parents[4]
P = A / "out/target/product/lt013g"
FC = P / "obj/ETC/file_contexts.bin_intermediates"
POLICY = P / "obj/ETC/sepolicy_intermediates/sepolicy"
BIN = A / "out/host/linux-x86/bin"
GOOD_FC = "b352e36032b4e82eaf6ba972a2a51eede2fe5be1feaf59964d0824f9e5638a1e"
GOOD_POLICY = "8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb"
GOOD_IMG = "1a83e6c15796ff9416f67fb22c49124a0729aa7337089a05063ab61369502635"
GOOD_CONTENT = {
    "system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service":
       "fe0cc1bb9ba1caed0603f4cc8175d593af20b1cd585ea69ffaaffb785d9fa075",
    "system/vendor/etc/selinux/vendor_sepolicy.cil":
       "a3b5dc65cbe28bca151fbfa3a0998b6c4d2262bceb65cf33c5881b8a39fe5dfe",
    "system/vendor/etc/selinux/precompiled_sepolicy": GOOD_POLICY,
    "sepolicy": GOOD_POLICY,
}

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while data := f.read(1048576):
            h.update(data)
    return h.hexdigest()

def must_sha(path: Path, sha: str) -> None:
    assert path.is_file() and digest(path) == sha, f"wrong SHA for {path}"

def run(*args: str, stdout=None) -> str:
    result = subprocess.run(args, check=True, text=True, stdout=stdout or subprocess.PIPE,
                            stderr=subprocess.PIPE)
    return result.stdout if stdout is None else ""

def create_labels() -> None:
    must_sha(POLICY, GOOD_POLICY)
    source = FC / "file_contexts.local.tmp"
    vendor = FC / "file_contexts.device.tmp"
    assert source.is_file() and vendor.is_file()
    for exe in ["checkfc", "fc_sort", "sefcontext_compile"]:
        assert (BIN / exe).is_file(), exe
    assert shutil.which("m4")
    with tempfile.TemporaryDirectory(prefix="g734-labels-", dir=FC) as temp:
        t = Path(temp)
        sorted_fc = t / "file_contexts.device.sorted.tmp"
        combined_fc = t / "file_contexts.concat.tmp"
        generated = t / "file_contexts.bin"
        run(str(BIN/"checkfc"), "-e", str(POLICY), str(vendor))
        run(str(BIN/"fc_sort"), "-i", str(vendor), "-o", str(sorted_fc))
        with combined_fc.open("w") as out:
            run("m4", "--fatal-warnings", "-s", str(source), str(sorted_fc), stdout=out)
        run(str(BIN/"checkfc"), str(POLICY), str(combined_fc))
        run(str(BIN/"sefcontext_compile"), "-o", str(generated), str(combined_fc))
        must_sha(generated, GOOD_FC)
        target = FC / "file_contexts.bin"
        if target.exists():
            must_sha(target, GOOD_FC)
        else:
            with target.open("xb") as out:
                out.write(generated.read_bytes())
            must_sha(target, GOOD_FC)
    print("G734 CANONICAL_ANDROID11_FILE_CONTEXTS_PASS", GOOD_FC)

def verify_image() -> None:
    must_sha(FC / "file_contexts.bin", GOOD_FC)
    must_sha(POLICY, GOOD_POLICY)
    sparse = P / "system.img"
    must_sha(sparse, GOOD_IMG)
    assert shutil.which("simg2img") and Path("/usr/sbin/debugfs").exists()
    with tempfile.TemporaryDirectory(prefix="g734-sparse-inspect-", dir=P) as temp:
        directory = Path(temp)
        raw = directory / "decoded-system.ext4"
        run("simg2img", str(sparse), str(raw))
        superblock = run("/usr/sbin/dumpe2fs", "-h", str(raw))
        assert "Filesystem state:         clean" in superblock
        for i, (internal, expected) in enumerate(GOOD_CONTENT.items()):
            dest = directory / f"embedded-{i:02d}"
            run("/usr/sbin/debugfs", "-R", f"dump /{internal} {dest}", str(raw))
            must_sha(dest, expected)
            print("PASS image file", internal, expected)
        xattrs = run("/usr/sbin/debugfs", "-R",
            "ea_list /system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service",
            str(raw))
        assert "security.selinux" in xattrs
    print("G734 VERIFIED_SPARSE_EXT4_SYSTEM_IMAGE", GOOD_IMG)
    print("NOT an OTA; no physical tablet updates performed")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--create-labels", action="store_true")
    group.add_argument("--verify-image", action="store_true")
    args = parser.parse_args()
    create_labels() if args.create_labels else verify_image()
