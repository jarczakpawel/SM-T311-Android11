#!/usr/bin/env python3
"""Read-only ELF compatibility report: old physical SM-T311 rild vs rebuilt rild.

Sections marked SHF_ALLOC are mapped at runtime. It is possible for AOSP's
unchanged executable to produce different ELF hashes due to the generated
.note.gnu.build-id and unallocated .gnu_debugdata. This tool DOES NOT claim
full-file identity or authorize flashing; it refuses any code/data mismatch.
Requires system readelf on the host; no device, credentials or network access.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

ENTRY="SYSTEM/vendor/bin/hw/rild"
PAT=re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\S+\s+(\S+)")
EXCLUDED={".note.gnu.build-id"}

def sections(blob:bytes)->dict:
    with tempfile.TemporaryDirectory(prefix="t311-elf-") as td:
        p=Path(td)/"rild";p.write_bytes(blob)
        output=subprocess.check_output(["readelf","--wide","-S",str(p)],text=True)
    result={}
    for line in output.splitlines():
        match=PAT.match(line)
        if not match:continue
        name,kind,address,offset,length,flags=match.groups()
        if "A" not in flags:continue
        start=int(offset,16);length=int(length,16)
        if kind!="NOBITS" and start+length>len(blob):
            raise ValueError("ELF section extends beyond EOF: "+name)
        content=b"" if kind=="NOBITS" else blob[start:start+length]
        result[name]={
            "type":kind,"address":address,"size":length,"flags":flags,
            "sha256":hashlib.sha256(content).hexdigest() if kind!="NOBITS" else None,
        }
    if not result or ".text" not in result or ".rodata" not in result:
        raise ValueError("Missing ELF code/rodata sections")
    return result

def audit(reference:bytes,candidate:bytes)->dict:
    old=sections(reference);new=sections(candidate)
    missing=sorted(old.keys()-new.keys());extra=sorted(new.keys()-old.keys())
    changed=sorted(n for n in old.keys()&new.keys() if old[n]!=new[n])
    significant=sorted(n for n in changed if n not in EXCLUDED)
    okay=not missing and not extra and not significant
    return {
        "status":"PASS_RUNTIME_ALLOC_IDENTICAL_BUILD_ID_ONLY" if okay else "BLOCK_ALLOC_ELF_DIFFERENT",
        "full_file_sha256_equal":hashlib.sha256(reference).digest()==hashlib.sha256(candidate).digest(),
        "reference_sha256":hashlib.sha256(reference).hexdigest(),
        "candidate_sha256":hashlib.sha256(candidate).hexdigest(),
        "section_count":len(old),
        "differing_allocated_sections":changed,
        "significant_sections_different":significant,
        "missing_sections":missing,"added_sections":extra,
        "code_text_sha256":new[".text"]["sha256"],
        "rodata_sha256":new[".rodata"]["sha256"],
        "flash_ready":False,
        "interpretation":"Allocated code/data identical except build identifier; whole-file SHA still different" if okay else "Cannot substitute binary without review",
    }

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--reference",type=Path,required=True)
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument("--candidate",type=Path)
    group.add_argument("--target-files",type=Path)
    p.add_argument("--report",type=Path)
    a=p.parse_args()
    orig=a.reference.read_bytes()
    if a.target_files:
        with zipfile.ZipFile(a.target_files) as f:compiled=f.read(ENTRY)
    else:compiled=a.candidate.read_bytes()
    result=audit(orig,compiled)
    if a.report:a.report.write_text(json.dumps(result,indent=2)+"\n")
    print(result["status"],"allocated",result["section_count"],
          "different",result["differing_allocated_sections"])
    return 0 if result["status"].startswith("PASS_") else 2

if __name__=="__main__":sys.exit(main())
