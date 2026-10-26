#!/usr/bin/env python3
"""Prepare exact pinned 64MiB G586 gzip transfer slices for Android USB ADB.

This command never executes ADB, never writes block devices, and never
modifies an existing slice if its SHA256 disagrees with the gzip source.
"""
import hashlib
import json
import sys
from pathlib import Path
CHUNK=64*1024*1024
ZIP="5ac8e055d2ffa326a5b72e9590db6fbb556c3c61668fbd84ed174c56d6f00a34"
RAW="2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09"
if len(sys.argv)!=2:
    sys.exit("usage: prepare_transport64.py /host/G586_ARTIFACT")
dir=Path(sys.argv[1]).resolve()
manifest=json.loads((dir/"manifest.json").read_text())
if manifest["targetfiles_sha256"]!=ZIP or manifest["raw_system_sha256"]!=RAW or len(manifest["parts"])!=5:
    sys.exit("BLOCKED: wrong G586 manifest")
target=dir/"transport64"
target.mkdir(exist_ok=True)
files=[]
for p in manifest["parts"]:
    idx=p["part"]
    if not isinstance(idx,int) or idx not in range(5):
        sys.exit("BLOCKED: incorrect fragment index")
    gz=dir/f"chunk{idx}.raw.gz"
    hg=hashlib.sha256()
    with gz.open("rb") as src:
        i=0
        while True:
            block=src.read(CHUNK)
            if not block:break
            hg.update(block)
            name=f"chunk{idx}.gz.part{i:02d}"
            dest=target/name
            h=hashlib.sha256(block).hexdigest()
            if dest.is_file():
                if dest.stat().st_size!=len(block) or hashlib.sha256(dest.read_bytes()).hexdigest()!=h:
                    sys.exit(f"BLOCKED: existing staged file mismatch {name}")
            else:
                dest.write_bytes(block)
            files.append({"part":idx,"slice":i,"name":name,"size":len(block),"sha256":h})
            i+=1
    if hg.hexdigest()!=p["gzip_sha256"]:
        sys.exit(f"BLOCKED: source gzip {idx} hash mismatch")
if len(files)!=11 or sum(x["size"] for x in files)!=sum(p["gzip_bytes"] for p in manifest["parts"]):
    sys.exit("BLOCKED: incomplete G586 staging manifest")
assert all(0<x["size"]<=CHUNK for x in files)
name=target/"slice_manifest.json"
name.write_text(json.dumps({"zip_sha256":ZIP,"raw_sha256":RAW,"files":files},indent=2)+"\n")
print("G586_G586_TRANSPORT64_VALIDATED slices=11 bytes="+str(sum(f["size"] for f in files)))
