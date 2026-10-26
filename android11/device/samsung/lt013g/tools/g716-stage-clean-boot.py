#!/usr/bin/env python3
"""Stage the already built G716 CLEAN kernel BOOT for Android's offline packaging.

Checks independent manifest SHA and full boot size before staging. Does not
access ADB/TWRP, install a ROM, overwrite a different staged image or back up
the tablet. Artifact itself is ignored by Git; regenerate it from source.
"""
import argparse
import json
import os
import tempfile
from hashlib import sha256
from pathlib import Path

DIR = Path(__file__).resolve().parents[1]
STAGED = DIR/"prebuilt/BOOT-7.3.0-pjarczak-G716-CLEAN-HOST-ONLY-8MiB.img"

def h(data):return sha256(data).hexdigest()

def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--source",required=True,type=Path,
        help="G716 boot image built by kernel project tools/g716_pack_clean_boot.py")
    cli.add_argument("--manifest",required=True,type=Path,
        help="G716-CLEAN-BOOT-MANIFEST.json")
    args=cli.parse_args()
    source=args.source.read_bytes()
    meta=json.loads(args.manifest.read_text())
    if meta.get("stage")!="G716" or meta.get("flash_authorized") is not False:
        raise ValueError("Expected host-only G716 manifest")
    if meta.get("PAGE_RCU_FREE_compiled") is not False:
        raise ValueError("Port must be physically removed from new kernel")
    if len(source)!=8388608 or h(source)!=meta["image_sha256"]:
        raise ValueError("G716 BOOT SHA/size inconsistent with manifest")
    if h(source)!="5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1":
        raise ValueError("Pinned G716 release SHA mismatch, do NOT stage")
    if STAGED.exists():
        if STAGED.read_bytes()!=source:
            raise ValueError("Existing G716 staged file differs; refuse overwrite")
        print("G716 already staged with correct SHA",h(source))
        return
    STAGED.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=STAGED.parent,delete=False) as f:
        f.write(source)
        name=f.name
    os.replace(name,STAGED)
    if h(STAGED.read_bytes())!=meta["image_sha256"]:
        raise RuntimeError("G716 staged image changed")
    print("G716 staged as Android OFFLINE candidate ONLY",STAGED,h(source))
    print("NOT flashed: physical G650 and TWRP recovery unchanged")

if __name__=="__main__":
    main()
