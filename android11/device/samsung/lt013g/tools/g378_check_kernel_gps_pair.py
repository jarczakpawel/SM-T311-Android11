#!/usr/bin/env python3
"""Fail closed on SM-T311 G378 kernel GNSS-Binder compatibility.

Validate an *offline* target-files candidate boot.img embeds precisely the
source-built G378 zImage+DTB and ZIP contains the matching source-built
launcher. Check new kernel .config enables the fourth binderfs device.
Never write a device, issue ADB, touch modem, PIN, PUK, or flash.

An actual G378 kernel is a hardware change. A passing pair check does NOT
signify safety of a full ROM or SELinux acceptance; physical boot/voice/GNSS
verification remains REQUIRED.
"""
from __future__ import annotations
import argparse
from hashlib import sha256
import json
from pathlib import Path
import struct
from zipfile import ZipFile
import sys

EXPECTED='CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder,lt013g-gps"'
LAUNCHER='SYSTEM/vendor/bin/lt013g-gps-legacy-launcher-v195'
BOOT='BOOTABLE_IMAGES/boot.img'

def check_config(text:str,old_text:str|None=None)->dict:
    opts=lambda x: {line.split("=",1)[0]:line for line in x.splitlines()
                    if "=" in line and not line.startswith("#")}
    o=opts(text)
    valid=o.get("CONFIG_ANDROID_BINDER_DEVICES")==EXPECTED
    diffs=[]
    if old_text is not None:
        original=opts(old_text)
        diffs=sorted(k for k in o.keys()|original.keys()
                     if o.get(k)!=original.get(k))
        valid=valid and diffs==["CONFIG_ANDROID_BINDER_DEVICES"]
    return {"config_valid":valid,"config_only_diff_keys":diffs,
            "binder_devices":o.get("CONFIG_ANDROID_BINDER_DEVICES")}

def validate(zip_path:Path,zimage:Path,dtb:Path,config:Path,
             launcher:Path,old_config:Path|None=None)->dict:
    cfg=check_config(config.read_text(),old_config.read_text() if old_config else None)
    k=zimage.read_bytes()+dtb.read_bytes()
    elf=launcher.read_bytes()
    with ZipFile(zip_path) as zp:
        boot=zp.read(BOOT)
        lz=zp.read(LAUNCHER)
    if boot[:8]!=b"ANDROID!" or len(boot)<2048:
        raise ValueError("Expected Android BOOT v0")
    length,=struct.unpack_from("<I",boot,8)
    page,version=struct.unpack_from("<II",boot,36)
    if page!=2048 or version!=0 or length<=0 or page+length>len(boot):
        raise ValueError("Unexpected Android boot image header")
    payload=boot[page:page+length]
    ok_len=len(k)==length
    matched=ok_len and payload==k
    executable_matched=lz==elf
    return {
        "status":"PASS_OFFLINE_PAIR_NOT_FLASH_READY" if
        matched and executable_matched and cfg["config_valid"] else "BLOCK_KERNEL_GNSS_MISMATCH",
        "kernel_matches_source":matched,
        "kernel_length_matches":ok_len,
        "launcher_matches_source_build":executable_matched,
        "candidate_config":cfg,
        "boot_sha256":sha256(boot).hexdigest(),
        "kernel_sha256":sha256(payload).hexdigest(),
        "launcher_sha256":sha256(lz).hexdigest(),
        "zip_sha256":sha256(zip_path.read_bytes()).hexdigest(),
        "flash_ready":False,
        "physical_device_untouched":True,
        "cannot_prove_selinux_enforcing":True,
    }

def main():
    p=argparse.ArgumentParser()
    for f in ("target-files","zimage","dtb","config","launcher"):
        p.add_argument("--"+f,type=Path,required=True)
    p.add_argument("--old-config",type=Path)
    p.add_argument("--report",type=Path)
    a=p.parse_args()
    result=validate(a.target_files,a.zimage,a.dtb,a.config,a.launcher,a.old_config)
    if a.report:a.report.write_text(json.dumps(result,indent=2)+"\n")
    print(result["status"],"boot",result["boot_sha256"],
          "launcher",result["launcher_sha256"])
    return 0 if result["status"]=="PASS_OFFLINE_PAIR_NOT_FLASH_READY" else 2

if __name__=="__main__":sys.exit(main())
