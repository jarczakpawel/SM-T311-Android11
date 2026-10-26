#!/usr/bin/env python3
"""G1188: invalidate only obsolete host QA receipts after TWRP fstab source fix.

This does not modify Android system images, kernel, device, or user data.
No backups or rollback copies. Requires SHA-pinned obsolete release artifacts.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

AOSP=Path("/home/debian/android/lineage-18.1-sm-t311")
PRODUCT=AOSP/"out/target/product/lt013g"
PROJECT=Path("/home/debian/Projekty/samsung")
FSTAB=AOSP/"device/samsung/lt013g/rootdir/recovery-ota.fstab"
OLD={
 PRODUCT/"obj/PACKAGING/G1152-qualified-ROM-source-manifest.json":
 "e6d0997cdd8332252db06190e74e0d79cd500e6c2c709d5c26bfb7ccf82f8c49",
 PRODUCT/"obj/PACKAGING/G1152-qualified-target-files-qa.json":
 "d8815ccb0494faea5b1a617a796a632fb997128bd5d171b3c9c8b51744796069",
 PROJECT/"artifacts/G1184-verified-packaged-system-finalizer-terminal.json":
 "3a709b967fa4bf223950ae12accf2e6833f7aa990a1ec068a90aa3f10ae63ea1",
}

def refuse(message):
 raise SystemExit("G1188_REFUSE: "+message)

def main():
 if sys.argv[1:] != ["--old-recovery-ota-fstab-mismatch-observed"]:
  refuse("requires actual old signed installer mismatch diagnosis")
 f=FSTAB.read_text()
 if f.count("/dev/block/platform/dw_mmc/by-name/") != 6 or "/dev/block/platform/soc/12550000.mmc/" in f:
  refuse("accepted TWRP recovery aliases not source-committed")
 current=subprocess.run(["git","-C",str(AOSP/"device/samsung/lt013g"),
                         "rev-parse","HEAD"],capture_output=True,text=True,check=True).stdout.strip()
 committed=subprocess.run(["git","-C",str(AOSP/"device/samsung/lt013g"),
                           "show","HEAD:rootdir/recovery-ota.fstab"],
                          capture_output=True,text=True,check=True).stdout
 if committed != f:
  refuse("accepted legacy recovery fstab does not match committed source")
 for file,sha in OLD.items():
  if file.is_symlink() or not file.is_file():
   refuse("expected exact obsolete receipt missing or symlinked: "+str(file))
  digest=hashlib.sha256(file.read_bytes()).hexdigest()
  if digest != sha: refuse("obsolete receipt mutated: "+str(file))
 old=json.loads(next(iter(OLD)).read_text())
 if (old.get("status") != "HOST_OFFLINE_RELEASE_QUALIFIED_NOT_PHYSICALLY_INSTALLED"
    or old.get("source_repo_commits",{}).get("android_device_lt013g") == current):
  refuse("old manifest not demonstrably stale after source commit")
 if (PROJECT/"artifacts/SM-T311-G1164-G1148-TWRP-OTA.zip").exists():
  refuse("a signed published OTA exists; never revoke while possible device install")
 for file in OLD:
  os.unlink(file)
 print("PASS G1188_ONLY_THREE_SHA_PINNED_OBSOLETE_HOST_RECEIPTS_REVOKED")
 print("G1188_NO_DEVICE_FLASH_NO_BACKUP_NO_SYSTEM_IMAGE_DELETE")

if __name__=="__main__":
 main()
