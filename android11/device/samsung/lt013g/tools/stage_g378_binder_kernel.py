#!/usr/bin/env python3
"""Stage GPL G378 ARM zImage+DTB for the matching GNSS device in local Git-ignored prebuilt.

Require ONLY binder device list change compared to physically verified G373
config, including machine-code string verification. Does NOT assert the new
kernel was physically tested. It does not flash, access ADB or SIM.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile

try:
    from g378_check_kernel_gps_pair import check_config,EXPECTED
except ImportError as exc:
    raise RuntimeError("Missing G378 release gate source") from exc

def stage(zimage:Path,dtb:Path,vmlinux:Path,config:Path,old_config:Path,
          target:Path,private_root:Path):
    target=target.resolve()
    if target.parent!=private_root.resolve():
        raise ValueError("Candidate kernel may only go in SM-T311 ignored prebuilt")
    cfg=check_config(config.read_text(),old_config.read_text())
    if not cfg["config_valid"] or cfg["config_only_diff_keys"]!=["CONFIG_ANDROID_BINDER_DEVICES"]:
        raise ValueError("Kernel configuration differs beyond GNSS Binder addition")
    if EXPECTED.split('"',2)[1].encode() not in vmlinux.read_bytes():
        raise ValueError("Compiled vmlinux lacks GNSS Binder parameter")
    z=zimage.read_bytes();d=dtb.read_bytes()
    if not z or not d or len(z)<4000000 or not d.startswith(b"\xd0\x0d\xfe\xed"):
        raise ValueError("Missing or invalid zImage / FDT")
    k=z+d
    if target.exists():
        if target.read_bytes()!=k:raise ValueError("Previously staged kernel differs")
        state="PASS_ALREADY_IDENTICAL"
    else:
        private_root.mkdir(parents=True,exist_ok=True)
        fd,name=tempfile.mkstemp(prefix=".g378-kernel-",dir=str(private_root))
        try:
            with os.fdopen(fd,"wb") as f:
                f.write(k);f.flush();os.fsync(f.fileno())
            os.replace(name,target)
        finally:
            if os.path.exists(name):os.unlink(name)
        state="PASS_STAGE_GPL_SOURCE_KERNEL"
    return {"status":state,"sha256":hashlib.sha256(k).hexdigest(),
            "bytes":len(k),"flash_ready":False,
            "not_tested_physically":True}

def main():
    p=argparse.ArgumentParser()
    for f in ("zimage","dtb","vmlinux","config","old-config","out"):
        p.add_argument("--"+f,type=Path,required=True)
    a=p.parse_args()
    private_root=Path(__file__).resolve().parents[1]/"prebuilt"
    result=stage(a.zimage,a.dtb,a.vmlinux,a.config,a.old_config,a.out,private_root)
    print(result["status"],result["bytes"],result["sha256"],
          "FLASH_READY",result["flash_ready"])
if __name__=="__main__":main()
