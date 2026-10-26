#!/usr/bin/env python3
"""Stage a GPL Linux kernel zImage+DTB from source outputs for SM-T311 only.

The verified local prebuilt is deliberately ignored by Git. Original stock
Samsung firmware/proprietary binaries are NEVER staged for public publishing.
Host only; never ADB, touch SIM, flash modem, or change any board other than
a caller-selected local file under the SM-T311 device/prebuilt directory.

Examples:
python3 device/samsung/lt013g/tools/stage_verified_kernel.py \
 --zimage /path/to/kernel/arch/arm/boot/zImage \
 --dtb /path/to/kernel/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb \
 --boot /path/to/verified-G373-BOOT.img \
 --out device/samsung/lt013g/prebuilt/kernel-g373-6.12.112-zimage-dtb.bin
"""
from __future__ import annotations

import argparse
import hashlib
import os
import struct
import tempfile
from pathlib import Path

def verify_parts(zimage: bytes, dtb: bytes, boot: bytes)->bytes:
    if not zimage or not dtb or len(boot)<2048 or boot[:8]!=b"ANDROID!":
        raise ValueError("No valid Linux zImage, DTB or Android BOOT")
    ksize, = struct.unpack_from("<I",boot,8)
    page,version = struct.unpack_from("<II",boot,36)
    if page!=2048 or version!=0 or ksize<=0 or 2048+ksize>len(boot):
        raise ValueError("Unexpected original SM-T311 Android BOOT v0 geometry")
    kernel=zimage+dtb
    if kernel!=boot[2048:2048+ksize]:
        raise ValueError("Source-built zImage+DTB differs from verified BOOT")
    return kernel

def stage(zimage:Path,dtb:Path,boot:Path,target:Path,base:Path)->dict:
    target=target.resolve()
    base=base.resolve()
    if target.parent!=base:
        raise ValueError("Only local SM-T311 ignored device/prebuilt directory allowed")
    kernel=verify_parts(zimage.read_bytes(),dtb.read_bytes(),boot.read_bytes())
    sha=hashlib.sha256(kernel).hexdigest()
    if target.exists():
        if target.read_bytes()!=kernel:
            raise ValueError("Existing staged kernel differs; explicit cleanup required")
        return {"status":"PASS_ALREADY_IDENTICAL","sha256":sha,"bytes":len(kernel)}
    base.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=".t311-kernel-",dir=str(base))
    try:
        with os.fdopen(fd,"wb") as f:
            f.write(kernel);f.flush();os.fsync(f.fileno())
        os.replace(temp,target)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return {"status":"PASS_STAGED_SOURCE_KERNEL","sha256":sha,"bytes":len(kernel)}

def main():
    parser=argparse.ArgumentParser()
    for field in ("zimage","dtb","boot","out"):parser.add_argument("--"+field,required=True,type=Path)
    args=parser.parse_args()
    path=Path(__file__).resolve().parents[1]/"prebuilt"
    result=stage(args.zimage,args.dtb,args.boot,args.out,path)
    print(result["status"],result["bytes"],result["sha256"])

if __name__=="__main__":main()
