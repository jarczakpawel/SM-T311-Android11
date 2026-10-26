#!/usr/bin/env python3
"""G732 offline coherence bundle; deliberately NOT an OTA or flashing script.

Packages the four already-built, SHA-pinned Android 11 / SM-T311 OTG
evaluation files with a manifest for reproducible source checkout.
No direct system/BOOT writes, ADB operations or device interaction.
"""
from __future__ import annotations
import hashlib,json,zipfile
from pathlib import Path

A=Path(__file__).resolve().parents[4]
P=Path("/home/debian/Projekty/samsung/linux-7.3-port")
DEST=P/"artifacts/G732-OTG-COHERENT-HOST-BUNDLE-NOT-FLASHABLE"
OUTPUT=DEST/"SM-T311-G732-G730-OTG-AND-G731-HAL-SEPOLICY-HOST-ONLY.zip"

entries={
 "BOOT-G730-OTG.img": (
    P/"artifacts/G730-OTG-ARM32-TEST-ONLY/BOOT-G730-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img",
    "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
 ),
 "vendor/bin/hw/android.hardware.usb.gadget@1.1-service": (
    A/"out/target/product/lt013g/system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service",
    "1011b1db80b1b6e652ab3a0a1772407976973f51be007af3487347024782a5ca"
 ),
 "vendor/etc/selinux/vendor_sepolicy.cil": (
    A/"out/target/product/lt013g/system/vendor/etc/selinux/vendor_sepolicy.cil",
    "a3b5dc65cbe28bca151fbfa3a0998b6c4d2262bceb65cf33c5881b8a39fe5dfe"
 ),
 "vendor/etc/selinux/precompiled_sepolicy": (
    A/"out/target/product/lt013g/system/vendor/etc/selinux/precompiled_sepolicy",
    "8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb"
 )
}
def sha(b):return hashlib.sha256(b).hexdigest()

readme="""SM-T311 G732 / G730 + G731 OTG EVALUATION — NOT FLASHABLE
================================================================
This ZIP is a host-side coherent binary collection, NOT a signed OTA,
not a recovery installable ZIP, and not authorization to overwrite
SYSTEM/BOOT, reboot into TWRP or drive VBUS without supervision.

Physical SM-T311 currently runs G727 with 3G modem, USB ADB/MTP working.
This bundle holds a newer ARM32 BOOT G730 (8MiB, original G650 ramdisk),
G731 Android USB gadget HAL, vendor SELinux CIL and precompiled policy.
It is a useful offline integration checkpoint after a full Android OTA
build unexpectedly required ~60,322 targets.

Source commits:
- kernel experimental G730 43cd66fc3
- HAL 50ce5991d
- common sepolicy 812d046
- device G731 425eea2
- device G732 boot selector ed9ebfd
- physical current BOOT G727 SHA dcc7aafe...57f9bc
Expected:
- No automatic OTG BOOST without a correct USB host state
- Preserve XMM6262 internal HSIC modem 1519:0020
- AOSP SELinux always Enforcing
- Old G484 enumerated thumbdrive 24a9:205a as /dev/sda1
- Correct USB host 5V and overcurrent NOT yet tested on G730/G731

No updater-script, update-binary or raw flash commands included.
The vendor paths are documentation of origin, not Android live writes.
All component SHA256s in MANIFEST.json.
"""
def main():
    files={}
    for key,(f,expected) in entries.items():
        b=f.read_bytes()
        assert sha(b)==expected,(key,sha(b),expected)
        files[key]=b
    assert len(files["BOOT-G730-OTG.img"])==8388608
    man={
        "stage":"G732","device":"SM-T311","date":"2026-10-21",
        "install_authorized":False,"flashable_ota":False,
        "hardware_otg_5v_verified":False,
        "physical_current_boot":"G727",
        "kernel_source_commit":"43cd66fc3",
        "android_hal_source_commit":"50ce5991d",
        "android_sepolicy_source_commit":"812d046",
        "files":{key:{"sha256":sha(b),"size":len(b)}
                 for key,b in sorted(files.items())}
    }
    files["MANIFEST.json"]=(json.dumps(man,indent=2,ensure_ascii=False)+"\n").encode()
    files["README-NOT-FLASHABLE.txt"]=readme.encode()
    DEST.mkdir(parents=True,exist_ok=True)
    if OUTPUT.exists():
        with zipfile.ZipFile(OUTPUT) as z:
            for key,b in files.items():
                assert z.read(key)==b,key
    else:
        with zipfile.ZipFile(OUTPUT,"x",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for key,b in sorted(files.items()):
                info=zipfile.ZipInfo(key,date_time=(2026,10,21,0,0,0))
                info.compress_type=zipfile.ZIP_DEFLATED
                info.external_attr=(0o100644<<16)
                z.writestr(info,b,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    with zipfile.ZipFile(OUTPUT) as z:
        assert not any(x.startswith("META-INF/com/google/android/") for x in z.namelist())
        for key,b in files.items():
            assert sha(z.read(key))==sha(b),key
    print("G732 HOST_BUNDLE_VERIFIED",OUTPUT)
    print("G732 SHA256",sha(OUTPUT.read_bytes()),"size",OUTPUT.stat().st_size)
    print("NOT FLASHABLE; physical tablet unchanged")
if __name__=="__main__":main()
