#!/usr/bin/env python3
"""G367: stage coherent G365 SystemUI ART pair in an independent target-files ZIP.

This is an *offline staging operation*. Never modifies source target-files,
installed tablet, BOOT/kernel, RIL, or SELinux policy. The staged ZIP omits
IMAGES/system.img and IMAGES/system.map on purpose: run the hermetic Android
add_img_to_target_files -a to regenerate them from staged SYSTEM contents.

Fail closed on incorrect hashes, unexpected files, or mismatched DEX CRC.
Never label output flash-ready: G367 still has unresolved BOOT/rild/SELinux gates.
"""
import argparse
import hashlib
import io
import json
import struct
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

SOURCE_SHA = "22ebba02be81fb65edec48b0f68a3c82be89b8908d3cedd93aa1db22f1f40bb9"
EXPECTED = {
    "SYSTEM/system_ext/priv-app/SystemUI/SystemUI.apk":
        "afc6f70e6be16a3f840dd47cfccb76ca9e649e2048a78baefcb595b15d433f95",
    "SYSTEM/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex":
        "c438feb53cc3bd0482f293d14fe7dee1b0659bd52391f70179cba24265a9b50f",
    "SYSTEM/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex":
        "b7a082b9f901af3d7459d4ff56c371e1a30a8530904faa6a354128ff8effe102"
}
OMITTED_IMGS = {"IMAGES/system.img", "IMAGES/system.map"}
EXTRA_PATHS = {
    "system/system_ext/priv-app/SystemUI/oat": "755",
    "system/system_ext/priv-app/SystemUI/oat/arm": "755",
    "system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex": "644",
    "system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex": "644"
}
MANIFEST = "META/g367-integrity-staging.json"

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            b = f.read(1024*1024)
            if not b: break
            h.update(b)
    return h.hexdigest()

def validate_vdex(apk, vdex):
    with ZipFile(apk) as z:
        dex_crc=z.getinfo("classes.dex").CRC
        dex_names=[n for n in z.namelist() if n.endswith(".dex")]
    blob=vdex.read_bytes()
    if dex_names!=["classes.dex"] or len(blob)<32 or blob[:8]!=b"vdex021\x00" or struct.unpack_from("<I",blob,12)[0]!=1:
        raise ValueError("Unexpected SystemUI ART format")
    got=struct.unpack_from("<I",blob,28)[0]
    if got!=dex_crc:raise ValueError("G367 APK/VDEX mismatch")
    return f"{dex_crc:08x}"

def main():
    args=argparse.ArgumentParser()
    args.add_argument("--source",required=True,type=Path)
    args.add_argument("--output",required=True,type=Path)
    args.add_argument("--apk",required=True,type=Path)
    args.add_argument("--odex",required=True,type=Path)
    args.add_argument("--vdex",required=True,type=Path)
    a=args.parse_args()
    if a.output.exists():raise ValueError("Destination already exists")
    if sha(a.source)!=SOURCE_SHA:raise ValueError("Unexpected base G366 target-files SHA")
    overlay={
        "SYSTEM/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex": a.odex,
        "SYSTEM/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex": a.vdex
    }
    for entry,p in overlay.items():
        if sha(p)!=EXPECTED[entry]:raise ValueError("Unexpected SystemUI pair SHA "+entry)
    if sha(a.apk)!=EXPECTED["SYSTEM/system_ext/priv-app/SystemUI/SystemUI.apk"]:
        raise ValueError("Unexpected SystemUI APK SHA")
    dex_crc=validate_vdex(a.apk,a.vdex)
    manifest={
        "stage":"G367_OFFLINE_NOT_FLASHABLE",
        "base_sha256":SOURCE_SHA,
        "systemui_dex_crc32":dex_crc,
        "injected_sha256":{k:EXPECTED[k] for k in overlay},
        "unaltered_boot":True,
        "unaltered_rild":True,
        "unaltered_selinux":True,
        "removed_stale_images":sorted(OMITTED_IMGS),
        "must_rebuild_system_img":True,
        "known_release_blockers":[
            "G333 kernel differs from G366 compiled BOOT",
            "rild full SHA differs in non-code metadata",
            "four SELinux permissive domains"
        ]
    }
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(a.source,"r") as src, ZipFile(a.output,"w",allowZip64=True) as dest:
        names=src.namelist()
        if len(names)!=len(set(names)):raise ValueError("Source contains duplicate ZIP entries")
        for name in overlay:
            if name in names:raise ValueError("Source already includes preopt file "+name)
        if not OMITTED_IMGS.issubset(names):
            raise ValueError("Expected old images absent")
        required={"META/filesystem_config.txt","SYSTEM/system_ext/priv-app/SystemUI/SystemUI.apk","BOOT/kernel","BOOT/cmdline","IMAGES/boot.img"}
        if not required.issubset(names):raise ValueError("Source ZIP missing mandatory content")
        actual_apk=src.read("SYSTEM/system_ext/priv-app/SystemUI/SystemUI.apk")
        if hashlib.sha256(actual_apk).hexdigest()!=EXPECTED["SYSTEM/system_ext/priv-app/SystemUI/SystemUI.apk"]:
            raise ValueError("Incorrect source SystemUI APK")
        for info in src.infolist():
            if info.filename in OMITTED_IMGS:continue
            if info.filename=="META/filesystem_config.txt":
                old=src.read(info)
                text=old.decode("utf-8")
                for path in EXTRA_PATHS:
                    if any(line.split(" ",1)[0]==path for line in text.splitlines()):
                        raise ValueError("Metadata already has "+path)
                if not text.endswith("\n"):text+="\n"
                text+="".join(f"{p} 0 0 {mode} selabel=u:object_r:system_file:s0 capabilities=0x0\n"
                              for p,mode in EXTRA_PATHS.items())
                dest.writestr(info,text.encode())
            else:
                # Preserve ZIP member mode, timestamp, compression and path.
                dest.writestr(info,src.read(info))
        for name,path in overlay.items():
            info=ZipInfo(name,date_time=(2009,1,1,0,0,0))
            info.compress_type=ZIP_DEFLATED
            info.external_attr=(0o100644<<16)
            dest.writestr(info,path.read_bytes())
        info=ZipInfo(MANIFEST,date_time=(2009,1,1,0,0,0))
        info.compress_type=ZIP_DEFLATED
        dest.writestr(info,json.dumps(manifest,indent=2)+"\n")
    with ZipFile(a.output) as z:
        if z.testzip() is not None:raise ValueError("Staged ZIP CRC damaged")
        if any(name in z.namelist() for name in OMITTED_IMGS):raise ValueError("Stale SYSTEM IMG retained")
        for name,value in EXPECTED.items():
            if hashlib.sha256(z.read(name)).hexdigest()!=value:
                raise ValueError("Staged SystemUI inconsistent")
    print("G367_STAGE_PASS",str(a.output),"sha256",sha(a.output),"dex_crc",dex_crc,flush=True)
    return 0

if __name__=="__main__":
    raise SystemExit(main())
