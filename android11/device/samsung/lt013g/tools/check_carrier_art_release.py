#!/usr/bin/env python3
"""Fail closed on Android 11 SM-T311 telephony-common/boot-ART release mismatch.

Changing telephony-common.jar without regenerated ART boot artifacts causes
bootclasspath dex/oat inconsistency. This host-only tool checks target-files
candidates versus a known coherent reference target-files; it never flashes,
writes, accesses SIM or invokes ADB.

Use: check_carrier_art_release.py --baseline G367.zip --candidate G374.zip
When jar bytes differ, refuse unless all boot-telephony-common.art/oat/vdex
and the corresponding new jars exist. This is a conservative release gate,
NOT proof of full runtime ART acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
import zipfile
from pathlib import Path

ROOT="SYSTEM/framework/"
BOOTPAIR=tuple(ROOT+"arm/boot-telephony-common."+ext for ext in ("art","oat","vdex"))
JAR=ROOT+"telephony-common.jar"
PATHS=(JAR,)+BOOTPAIR

def get_digest(z:zipfile.ZipFile, key:str) -> str|None:
    try:
        with z.open(key) as fh:
            h=hashlib.sha256()
            for part in iter(lambda:fh.read(1024*1024),b""):
                h.update(part)
        return h.hexdigest()
    except KeyError:return None

def inspect(reference:Path,candidate:Path,require_change:bool=False)->dict:
    with zipfile.ZipFile(reference) as ref,zipfile.ZipFile(candidate) as out:
        a={k:get_digest(ref,k) for k in PATHS}
        b={k:get_digest(out,k) for k in PATHS}
        missing=[k for k in PATHS if b[k] is None]
        changed={k:(a[k]!=b[k]) for k in PATHS}
        jar_changed=changed[JAR]
        pair_changed=any(changed[k] for k in BOOTPAIR)
        if missing:status="BLOCK_MISSING_BOOTCLASSPATH_COMPONENT"
        elif jar_changed and not pair_changed:status="BLOCK_STALE_BOOT_ART"
        elif require_change and not jar_changed:status="BLOCK_NO_NEW_TELEPHONY_JAR"
        elif jar_changed and pair_changed:status="ART_CHANGED_REQUIRES_FULL_RUNTIME_ACCEPTANCE"
        else:status="BASELINE_UNMODIFIED_NOT_NEW_RELEASE"
        return {
            "status":status, "jar_changed":jar_changed, "boot_pair_changed":pair_changed,
            "baseline":str(reference),"candidate":str(candidate),
            "missing":missing,
            "sha256_old":a,"sha256_new":b,"changed":changed,
            "flash_ready":False, "checks_only_art_pair_not_rom":True,
        }

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--baseline",required=True,type=Path)
    p.add_argument("--candidate",required=True,type=Path)
    p.add_argument("--require-change",action="store_true")
    p.add_argument("--json",type=Path)
    a=p.parse_args()
    result=inspect(a.baseline,a.candidate,a.require_change)
    if a.json:a.json.write_text(json.dumps(result,indent=2)+"\n")
    print(result["status"])
    print("telephony-common SHA",result["sha256_new"][JAR],
          "old",result["sha256_old"][JAR])
    print("boot ART changed",result["boot_pair_changed"],"missing",result["missing"])
    return 0 if result["status"]=="ART_CHANGED_REQUIRES_FULL_RUNTIME_ACCEPTANCE" else 2

if __name__=="__main__":sys.exit(main())
