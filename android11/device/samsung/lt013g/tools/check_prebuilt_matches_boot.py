#!/usr/bin/env python3
"""Verify the SM-T311 product prebuilt kernel is EXACTLY the proven BOOT kernel.

Only reads local files. No ADB, flashing, modem/SIM, network or privileges.
Use alongside target-files validation to reject a stale product prebuilt.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

def inspect(prebuilt:Path, image:Path)->dict:
    b=image.read_bytes()
    p=prebuilt.read_bytes()
    if len(b)<2048 or b[:8]!=b"ANDROID!":
        raise ValueError("Expected Android BOOT v0 image")
    values=struct.unpack_from("<10I",b,8)
    ksize=values[0]
    page=values[7]
    version=values[8]
    if page!=2048 or version!=0 or ksize<=0 or 2048+ksize>len(b):
        raise ValueError("Unexpected SM-T311 BOOT layout")
    kernel=b[page:page+ksize]
    equal=p==kernel
    sha=lambda x:hashlib.sha256(x).hexdigest()
    return {
        "status":"PASS_EXACT_KERNEL" if equal else "BLOCK_PRODUCT_KERNEL_MISMATCH",
        "prebuilt_sha256":sha(p),
        "boot_kernel_sha256":sha(kernel),
        "whole_boot_sha256":sha(b),
        "kernel_length":len(kernel),
        "prebuilt_length":len(p),
        "equal":equal,
        "flash_ready":False,
    }

def main():
    pa=argparse.ArgumentParser()
    pa.add_argument("--prebuilt",required=True,type=Path)
    pa.add_argument("--boot",required=True,type=Path)
    pa.add_argument("--report",type=Path)
    args=pa.parse_args()
    result=inspect(args.prebuilt,args.boot)
    print(result["status"],result["prebuilt_sha256"],result["boot_kernel_sha256"])
    if args.report:args.report.write_text(json.dumps(result,indent=2)+"\n")
    return 0 if result["equal"] else 2

if __name__=="__main__":
    sys.exit(main())
