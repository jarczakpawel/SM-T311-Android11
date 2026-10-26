#!/usr/bin/env python3
"""Read-only Android boot v0 cross-check of proven G333 and proposed G367.

Both image sources are stored ZIP entries; no ADB, modem, block device,
kernel flash, or filesystem write except requested JSON report.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from zipfile import ZipFile

def digest(blob):
    return hashlib.sha256(blob).hexdigest()

def boot_segments(data):
    if len(data)<2048 or data[:8]!=b"ANDROID!":
        raise ValueError("Not a legacy Android BOOT image")
    ksize,kaddr,rsize,raddr,ssize,saddr,tags,pagesize,headerver,osversion=struct.unpack_from("<10I",data,8)
    if headerver!=0 or pagesize not in (2048,4096,8192):
        raise ValueError("Unexpected BOOT header/page size")
    pos=pagesize
    def take(length):
        nonlocal pos
        result=data[pos:pos+length]
        if len(result)!=length:raise ValueError("Boot segment truncated")
        pos+=((length+pagesize-1)//pagesize)*pagesize
        return result
    kern=take(ksize)
    ram=take(rsize)
    sec=take(ssize)
    if pos!=len(data):raise ValueError("Unrecognized BOOT trailer; refuse assumptions")
    cmdline=data[64:64+512].split(b"\x00",1)[0].decode(errors="replace")
    return {
        "raw_sha256":digest(data),"bytes":len(data),"kernel_sha256":digest(kern),
        "kernel_bytes":len(kern),"ramdisk_sha256":digest(ram),
        "ramdisk_bytes":len(ram),"second_sha256":digest(sec),
        "second_bytes":len(sec),"page_size":pagesize,
        "cmdline":cmdline,"kernel_addr":kaddr,"ramdisk_addr":raddr,
        "tags_addr":tags,"osversion_raw":osversion
    }

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--g333",required=True,type=Path)
    p.add_argument("--g367",required=True,type=Path)
    p.add_argument("--report",required=True,type=Path)
    a=p.parse_args()
    with ZipFile(a.g333) as z: old=boot_segments(z.read("payload/BOOT-G333.img"))
    with ZipFile(a.g367) as z:
        new=boot_segments(z.read("IMAGES/boot.img"))
        packaged_kernel=z.read("BOOT/kernel")
        packaged_cmdline=z.read("BOOT/cmdline").decode().strip()
    if digest(packaged_kernel)!=new["kernel_sha256"]:
        raise ValueError("G367 BOOT/kernel != IMAGES/boot.img kernel")
    if packaged_cmdline!=new["cmdline"]:
        raise ValueError("G367 BOOT/cmdline != packaged header cmdline")
    kernel_same=old["kernel_sha256"]==new["kernel_sha256"]
    ramdisk_same=old["ramdisk_sha256"]==new["ramdisk_sha256"]
    report={
        "status":"IDENTICAL" if kernel_same and ramdisk_same else
                 "KERNEL_DIFF_RAMDISK_SAME" if ramdisk_same else "KERNEL_OR_RAMDISK_DIFF",
        "g333_live":old,"g367_candidate":new,
        "kernel_same":kernel_same,"ramdisk_same":ramdisk_same,
        "cmdline_same":old["cmdline"]==new["cmdline"],
        "g367_boot_source_parts_consistent":True,
        "flash_authorized":False,
        "remarks":"Hardware regression tests required before any BOOT change."
    }
    a.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print("G367_BOOT_AUDIT",report["status"],
          "G333",old["kernel_bytes"],"G367",new["kernel_bytes"],
          "RAMDISK_IDENTICAL",ramdisk_same,"REPORT",a.report)
    return 0

if __name__=="__main__":
    raise SystemExit(main())
