#!/usr/bin/env python3
"""Read-only G367 ext4 contents validator for regenerated SYSTEM image.

Converts compressed sparse image to a private raw file, extracts *only* three
SystemUI artifacts with debugfs, verifies full expected SHA, then removes
gigabyte-sized temporary images. Never accesses adb or live block devices.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import shutil
from zipfile import ZipFile

EXPECTED = {
    "SystemUI.apk": "afc6f70e6be16a3f840dd47cfccb76ca9e649e2048a78baefcb595b15d433f95",
    "SystemUI.odex": "c438feb53cc3bd0482f293d14fe7dee1b0659bd52391f70179cba24265a9b50f",
    "SystemUI.vdex": "b7a082b9f901af3d7459d4ff56c371e1a30a8530904faa6a354128ff8effe102"
}

def sha_file(file):
    h=hashlib.sha256()
    with file.open("rb") as fd:
        for b in iter(lambda:fd.read(1024*1024),b""):h.update(b)
    return h.hexdigest()

def run(*args):
    proc=subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True,timeout=90)
    if proc.returncode!=0:
        raise RuntimeError("Command failed: "+" ".join(map(str,args))+
                           "\n"+proc.stdout[-2500:])
    return proc.stdout

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--zip",type=Path,required=True)
    ap.add_argument("--workdir",type=Path,required=True)
    ap.add_argument("--report",type=Path,required=True)
    ap.add_argument("--simg2img",type=Path,required=True)
    ap.add_argument("--debugfs",type=Path,required=True)
    a=ap.parse_args()
    a.workdir.mkdir(parents=True,exist_ok=True)
    sparse=a.workdir/"G367-VERIFY-system.sparse.img"
    raw=a.workdir/"G367-VERIFY-system.raw.img"
    if sparse.exists() or raw.exists():
        raise ValueError("Previous temporary image exists; do not overwrite")
    try:
        with ZipFile(a.zip) as z:
            with z.open("IMAGES/system.img") as fi,sparse.open("wb") as fo:
                shutil.copyfileobj(fi,fo,length=1024*1024)
        print("G367_SPARSE_EXTRACTED",sparse.stat().st_size,flush=True)
        run(str(a.simg2img),str(sparse),str(raw))
        print("G367_RAW_EXT4",raw.stat().st_size,flush=True)
        # Android system-as-root ext4 may store either /system/.. or /..
        roots=("/system_ext/priv-app/SystemUI",
               "/system/system_ext/priv-app/SystemUI")
        found={}
        for name,expected in EXPECTED.items():
            if name.endswith(".apk"):suffix="/SystemUI.apk"
            else:suffix="/oat/arm/"+name
            valid=False
            for root in roots:
                path=root+suffix
                stat=run(str(a.debugfs),"-R","stat "+path,str(raw))
                if "Inode:" not in stat:
                    continue
                attrs=run(str(a.debugfs),"-R","ea_list "+path,str(raw))
                if not re.search(r"Type:\s+regular\s+Mode:\s+0644",stat):
                    raise ValueError("Wrong file mode in ext4: "+name)
                if not re.search(r"User:\s+0\s+Group:\s+0",stat):
                    raise ValueError("Wrong file owner in ext4: "+name)
                if "security.selinux" not in attrs or "u:object_r:system_file:s0" not in attrs:
                    raise ValueError("Wrong SELinux xattr in ext4: "+name)
                extracted=a.workdir/("G367-VERIFY-"+name)
                if extracted.exists():raise ValueError("Extraction already exists")
                dump=run(str(a.debugfs),"-R",f"dump {path} {extracted}",str(raw))
                digest=sha_file(extracted)
                found[name]={"ext4_path":path,"sha256":digest,
                             "expected_sha256":expected,"bytes":extracted.stat().st_size,
                             "mode":"0644","uid":0,"gid":0,
                             "selinux":"u:object_r:system_file:s0"}
                extracted.unlink()
                if digest!=expected:
                    raise ValueError("Image artifact mismatch: "+name)
                valid=True
                print("IMAGE_FILE_PASS",name,digest,flush=True)
                break
            if not valid:raise ValueError("Expected SystemUI artifact absent in image: "+name)
        a.report.write_text(json.dumps(
            {"status":"PASS","system_img_validation":"OFFLINE_EXT4_FILE_HASH",
             "target_files":str(a.zip),"checked":found,"adb_writes":False},
            indent=2)+"\n")
        print("G367_SYSTEM_IMG_VERIFIED_PASS",flush=True)
    finally:
        for item in (sparse,raw):
            if item.exists():item.unlink()
    return 0

if __name__=="__main__":
    raise SystemExit(main())
