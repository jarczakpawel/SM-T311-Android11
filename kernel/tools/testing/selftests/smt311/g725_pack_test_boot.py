#!/usr/bin/env python3
"""G725 corrected current-limit OTG TEST BOOT packer for SM-T311.

Builds exact Android v0 8MiB image with original *G650* ramdisk and
known correct T311 device-tree. Test-only; no ADB/reboot/flash or backups.
The production G716 artifact and Android out/boot.img are NEVER touched.
"""
from __future__ import annotations
import importlib.util
import json
import os
import struct
import tempfile
from pathlib import Path
from hashlib import sha256

PORT=Path("/home/debian/Projekty/samsung/linux-7.3-port")
BUILD=PORT/"build/g724-otg-boost-gated-host-only"
PACK=PORT/"tools/g696_pack_release_off_boot.py"
OUT=PORT/"artifacts/G725-OTG-ARM32-TEST-ONLY"
BOOT=OUT/"BOOT-G725-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img"
MANIFEST=OUT/"G725-OTG-ARM32-TEST-MANIFEST.json"
SPEC=importlib.util.spec_from_file_location("g696_proven_boot_v0",PACK)
p=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)

def digest(b:bytes)->str:
    return sha256(b).hexdigest()

def make():
    base=p.BASE.read_bytes()
    original=p.unpack(base)
    if p.pack(original,original["kernel"])!=base:
        raise ValueError("G650 baseline failed byte-for-byte roundtrip")
    cfg=(BUILD/".config").read_text()
    release=(BUILD/"include/config/kernel.release").read_text().strip()
    if release!="7.3.0-pjarczak":
        raise ValueError("Unexpected kernel name")
    if ("CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y" not in cfg
        or "CONFIG_PAGE_RCU_FREE=y" in cfg
        or "CONFIG_USB_STORAGE=y" not in cfg
        or "CONFIG_EXTCON_MAX77693=y" not in cfg
        or "CONFIG_USB_EHCI_EXYNOS=y" not in cfg
        or "CONFIG_USB_OHCI_EXYNOS=y" not in cfg):
        raise ValueError("OTG opt-in board profile incorrect or unsafe RCU present")
    z=(BUILD/"arch/arm/boot/zImage").read_bytes()
    dt=(BUILD/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes()
    if digest(dt)!=p.DTB_SHA or dt[:4]!=bytes.fromhex("d00dfeed"):
        raise ValueError("SM-T311 DTB source not identical to G716 verified")
    if struct.unpack(">I",dt[4:8])[0]!=len(dt):
        raise ValueError("Malformed DTB")
    payload=z+dt
    candidate=p.pack(original,payload)
    checked=p.unpack(candidate,allow_corrupt=True)
    if (checked["kernel"]!=payload or checked["ramdisk"]!=original["ramdisk"] or
        checked["header"][12:576]!=original["header"][12:576] or
        checked["header"][596:]!=original["header"][596:]):
        raise ValueError("Unexpected RAMDISK or Android BOOT header mutation")
    if candidate==base or len(candidate)!=8388608:
        raise ValueError("Unexpected BOOT identity/size")
    result={
       "stage":"G725", "type":"SUPERVISED_OTG_EXPERIMENTAL",
       "date":"2026-10-21","status":"HOST_ONLY_NOT_INSTALLED",
       "boot_flash_authorized":False,
       "complete_otg_functionality_verified":False,
       "physical_5v_vbus_measured":False,
       "physical_overcurrent_and_recovery_verified":False,
       "test_kernel_config_otg_boost":True,
       "production_G716_kernel_config_otg_boost":False,
       "kernelrelease":release,
       "format":"Android BOOT v0",
       "image_size":len(candidate),
       "boot_sha256":digest(candidate),
       "vmlinux_sha256":digest((BUILD/"vmlinux").read_bytes()),
       "zimage_sha256":digest(z),
       "dtb_sha256":digest(dt),
       "ramdisk_sha256":digest(original["ramdisk"]),
       "source_G650_boot_sha256":digest(base),
       "production_G716_unchanged_sha256":"5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1",
       "safety_features":[
          "T311 board and DTS explicit opt-in",
          "MAX77693 ID and EXTCON_USB_HOST",
          "no simultaneous USB gadget/PHY0 and host",
          "EHCI+OHCI and MODE_SWITCH host required",
          "no charging/OTG conflict",
          "unqualified bit6 OTG current refused; stock 900mA bit7 allowed with BYP IRQ",
          "BYP overcurrent latched",
          "3s root-only refresh lease with watchdog",
          "reboot PMIC verified OFF attempt",
          "no automatic BOOST from cable attach alone"
       ]
    }
    OUT.mkdir(parents=True,exist_ok=True)
    if BOOT.exists() and BOOT.read_bytes()!=candidate:
        raise ValueError("Refusing to overwrite differing test BOOT")
    if MANIFEST.exists() and json.loads(MANIFEST.read_text())!=result:
        raise ValueError("Refusing to overwrite differing manifest")
    if not BOOT.exists():
        with tempfile.NamedTemporaryFile(dir=OUT,delete=False) as f:
            f.write(candidate)
            temp=f.name
        os.replace(temp,BOOT)
    if not MANIFEST.exists():
        with tempfile.NamedTemporaryFile("w",dir=OUT,delete=False) as f:
            f.write(json.dumps(result,indent=2,ensure_ascii=False)+"\n")
            temp=f.name
        os.replace(temp,MANIFEST)
    if digest(BOOT.read_bytes())!=result["boot_sha256"]:
        raise ValueError("Image failed post-write SHA")
    print("G725 TEST-ONLY BOOT 8MiB",result["boot_sha256"])
    print("G725 zImage",result["zimage_sha256"])
    print("G725 original G650 RAMDISK + G716 DTB preserved; NO FLASH")
if __name__=="__main__":
    make()
