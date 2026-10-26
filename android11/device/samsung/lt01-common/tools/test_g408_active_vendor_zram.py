#!/usr/bin/env python3
"""G408: source-only invariants of actual Android11 second-stage ZRAM setup.

Does not execute swapon, modify device or write partitions. Negative mutants
must fail without relying on old ramdisk fstab that G379 does not use.
"""
from pathlib import Path
from zipfile import ZipFile
import sys

ROOT = Path(__file__).resolve().parents[1]
AOSP = ROOT.parents[2] / "system/core"

def require(condition, reason):
    if not condition:
        raise AssertionError(reason)

def validate(fstab, rc):
    rows = [line.split() for line in fstab.splitlines()
            if line.strip() and not line.lstrip().startswith("#")]
    require(all(len(row) >= 5 for row in rows), "malformed fstab record")
    swaps = [r for r in rows if r[2] == "swap"]
    require(len(swaps) == 1, "exactly one swap entry required")
    require(swaps[0][:4] == ["/dev/block/zram0", "none", "swap", "defaults"],
            "no disk-backed swap or wrong zram path")
    flags = swaps[0][4].split(",")
    require(len(flags) == len(set(flags)), "duplicate flags")
    require(set(flags) == {"zramsize=805306368", "swapprio=32758", "wait"},
            "wrong 768 MiB zram flags or deprecated max_comp_streams")
    misc = [r for r in rows if r[1] == "/misc"]
    require(len(misc) == 1 and
            misc[0] == ["/dev/block/platform/soc/12550000.mmc/by-name/OTA",
                        "/misc", "emmc", "defaults", "defaults"],
            "factory PIT OTA p11 /misc entry altered")
    require(len([r for r in rows if r[1] == "/efs"]) == 1,
            "EFS existing mount altered")
    require(len([r for r in rows if r[1] == "/data"]) == 1,
            "userdata original mount missing")
    require(len([r for r in rows if r[1] == "/cache"]) == 1,
            "cache original mount missing")
    require(rc.count("    swapon_all /vendor/etc/fstab.lt013g") == 1,
            "active init must swapon_all actual vendor fstab once")
    require("on fs\n    mount_all /vendor/etc/fstab.lt013g\n"
            "    swapon_all /vendor/etc/fstab.lt013g" in rc,
            "swapon must follow real vendor mount_all in fs trigger")
    require("on boot\n    write /proc/sys/vm/page-cluster 0" in rc,
            "low-latency RAM-swap vm page-cluster policy missing")
    require("write /proc/sys/vm/swappiness" not in rc,
            "vendor_init cannot safely write generic proc swappiness label")
    forbidden = ("swapoff", "insmod", "reboot", "rm -rf", "dd ", "format ",
                 "exec_start smt311-zram")
    require(not any(cmd in rc for cmd in forbidden),
            "unexpected mutating init command")
    return True

def main():
    fstab = (ROOT/"rootdir/fstab.lt013g.vendor").read_text()
    rc = (ROOT/"rootdir/init.lt013g.vendor.rc").read_text()
    require(validate(fstab, rc), "source baseline failed")
    common = (ROOT/"common.mk").read_text()
    require("rootdir/fstab.lt013g.vendor:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.lt013g"
            in common, "actual vendor fstab not copied into ROM")
    require("rootdir/init.lt013g.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.lt013g.rc"
            in common, "actual second-stage init not copied")
    builtins = (AOSP/"init/builtins.cpp").read_text()
    mgr = (AOSP/"fs_mgr/fs_mgr.cpp").read_text()
    parser = (AOSP/"fs_mgr/fs_mgr_fstab.cpp").read_text()
    require("Result<void> do_swapon_all(" in builtins and
            "fs_mgr_swapon_all(fstab)" in builtins,
            "Android11 init swapon_all parser not found")
    require("bool fs_mgr_swapon_all(const Fstab& fstab)" in mgr and
            'entry.fs_type != "swap"' in mgr and
            "swapon(entry.blk_device.c_str(), flags)" in mgr,
            "Android11 fs_mgr swap implementation absent")
    require('"swapon_all",              {0,     1,    {false,  do_swapon_all}}' in builtins and
            '"write",                   {2,     2,    {true,   do_write}}' in builtins,
            "init vs vendor_init dispatch for swapon/write changed")
    require('StartsWith(flag, "zramsize=")' in parser and
            'StartsWith(flag, "swapprio=")' in parser,
            "fstab swap flags unsupported")
    cases = [
        ("no_zram", fstab.replace("/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait\n", ""), rc),
        ("no_swapon_all", fstab, rc.replace("    swapon_all /vendor/etc/fstab.lt013g\n", "")),
        ("old_ramdisk_path", fstab, rc.replace("    swapon_all /vendor/etc/fstab.lt013g", "    swapon_all /fstab.lt013g")),
        ("wrong_size", fstab.replace("805306368", "536870912"), rc),
        ("wrong_priority", fstab.replace("swapprio=32758", "swapprio=0"), rc),
        ("max_comp_streams", fstab.replace("swapprio=32758,wait", "max_comp_streams=2,swapprio=32758,wait"), rc),
        ("duplicate_swap", fstab + "/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait\n", rc),
        ("disk_swap", fstab.replace("/dev/block/zram0 none swap", "/dev/block/mmcblk0p21 none swap"), rc),
        ("misc_mutation", fstab.replace("by-name/OTA /misc", "by-name/EFS /misc"), rc),
        ("unsafe_auto_reboot", fstab, rc + "\non boot\n    reboot recovery\n"),
        ("missing_vm_policy", fstab, rc.replace("    write /proc/sys/vm/page-cluster 0", "")),
        ("forbidden_generic_proc_write", fstab, rc+"\n    write /proc/sys/vm/swappiness 100\n"),
    ]
    for name, f, r in cases:
        try:
            validate(f, r)
        except AssertionError:
            print("PASS fail-closed mutant:", name)
        else:
            raise AssertionError(f"unsafe mutant accepted: {name}")
    # Check against IMMUTABLE physically staged G403 reference instead of git
    # HEAD, which now includes G408 fixes and must not masquerade as G403.
    archived = Path("/home/debian/Projekty/samsung/artifacts/"
                    "SM-T311-G403-BOUNDED-CP-CLOSE-20261017/"
                    "G403-full-guarded-target-files.zip")
    require(archived.is_file(), "immutable real G403 reference target-files absent")
    with ZipFile(archived) as z:
        for rel,name in [
            ("rootdir/fstab.lt013g.vendor", "SYSTEM/vendor/etc/fstab.lt013g"),
            ("rootdir/init.lt013g.vendor.rc",
             "SYSTEM/vendor/etc/init/hw/init.lt013g.rc")
        ]:
            old = z.read(name).decode()
            try:
                validate(old if rel.endswith("fstab.lt013g.vendor") else fstab,
                         old if rel.endswith("init.lt013g.vendor.rc") else rc)
            except AssertionError:
                print("PASS archived actual G403 lacks active ZRAM:", rel)
            else:
                raise AssertionError("archived real G403 unexpectedly already fixed: "+rel)
    print("G408 SOURCE ONLY PASS: 12 mutants, 2 original G403 regressions, no physical changes")

if __name__ == "__main__":
    main()
