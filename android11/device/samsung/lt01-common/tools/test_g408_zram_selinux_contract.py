#!/usr/bin/env python3
"""Fail-closed Android11 zram SELinux source audit, no physical writes."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
AOSP=ROOT.parents[2]
POLICY=ROOT/"sepolicy/private/lt013g_g408_zram.te"
EXPECTED={
    "allow init ram_device:blk_file rw_file_perms;",
    "allow toolbox ram_device:blk_file rw_file_perms;",
}

def insist(ok,why):
    if not ok: raise AssertionError(why)

def check(src):
    lines={line.strip() for line in src.splitlines()
           if line.strip() and not line.lstrip().startswith("#")}
    insist(lines==EXPECTED,"unexpected/absent SELinux RAM block permissions")
    insist("swap_block_device" not in src.replace(
        "swap_block_device, not this built-in",""),
        "do not alter swap block relabeling in device policy")
    insist("neverallow" not in src and "permissive" not in src,
           "no permissive/neverallow policy rewrite")
    return True

def main():
    source=POLICY.read_text()
    insist(check(source),"positive zram policy failed")
    contexts=(AOSP/"system/sepolicy/private/file_contexts").read_text()
    insist("/dev/block/zram[0-9]*" in contexts and
           "u:object_r:ram_device:s0" in contexts,
           "Android11 real zram file_contexts label changed")
    init=(AOSP/"system/sepolicy/public/init.te").read_text()
    toolbox=(AOSP/"system/sepolicy/public/toolbox.te").read_text()
    insist("allow init swap_block_device:blk_file rw_file_perms;" in init,
           "standard init swap type changed")
    insist("allow toolbox swap_block_device:blk_file rw_file_perms;" in toolbox,
           "standard toolbox swap type changed")
    board=(ROOT/"BoardConfigCommon.mk").read_text()
    insist("BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/samsung/lt01-common/sepolicy/private" in board,
           "G408 rule source not integrated into platform private sepolicy")
    for name, bad in (
        ("no_toolbox",source.replace("allow toolbox ram_device:blk_file rw_file_perms;", "")),
        ("no_init",source.replace("allow init ram_device:blk_file rw_file_perms;", "")),
        ("unsafe_global_block",source+"\\nallow init block_device:blk_file rw_file_perms;\\n"),
        ("permissive",source+"\\npermissive init;\\n"),
        ("wrong_label",source.replace("ram_device:blk_file","swap_block_device:blk_file")),
        ("wrong_class",source.replace("ram_device:blk_file","ram_device:chr_file"))
    ):
        try:check(bad)
        except AssertionError:print("PASS reject unsafe policy mutant:",name)
        else:raise AssertionError("FAIL OPEN "+name)
    print("G408 PHYSICAL RAM_DEVICE SELINUX SOURCE CONTRACT PASS, six mutants rejected")
if __name__=="__main__":main()
