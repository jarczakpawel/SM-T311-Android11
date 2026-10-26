#!/usr/bin/env python3
"""G732: pin a BUILD-ONLY G730 OTG evaluation BOOT.

Do not flash, mount, reboot, modify vendor partitions, or mark it safe.
Both test manifest and compiler output must match; G716 remains default.
"""
from __future__ import annotations
import hashlib, json, os, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
PORT=Path("/home/debian/Projekty/samsung/linux-7.3-port")
ORIGIN=PORT/"artifacts/G730-OTG-ARM32-TEST-ONLY"
SRC=ORIGIN/"BOOT-G730-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img"
META=ORIGIN/"G730-OTG-ARM32-TEST-MANIFEST.json"
DEST=ROOT/"device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G730-OTG-EVAL-8MiB.img"
EXPECTED="e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"

def sha(data):return hashlib.sha256(data).hexdigest()

def main():
    d=json.loads(META.read_text())
    assert d["stage"]=="G730"
    assert d["boot_flash_authorized"] is False
    assert d["complete_otg_functionality_verified"] is False
    assert d["status"]=="HOST_ONLY_NOT_INSTALLED"
    assert d["kernelrelease"]=="7.3.0-pjarczak"
    assert d["image_size"]==8*1024*1024
    assert d["boot_sha256"]==EXPECTED
    b=SRC.read_bytes()
    assert len(b)==8*1024*1024 and sha(b)==EXPECTED and b.startswith(b"ANDROID!")
    kernel=PORT/"build/g724-otg-boost-gated-host-only"
    cfg=(kernel/".config").read_text()
    assert "CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y" in cfg
    assert "CONFIG_USB_STORAGE=y" in cfg
    assert "CONFIG_EXTCON_MAX77693=y" in cfg
    assert "CONFIG_USB_EHCI_EXYNOS=y" in cfg
    assert "CONFIG_USB_OHCI_EXYNOS=y" in cfg
    assert "CONFIG_PAGE_RCU_FREE=y" not in cfg
    assert (kernel/"include/config/kernel.release").read_text().strip()==d["kernelrelease"]
    assert sha((kernel/"arch/arm/boot/zImage").read_bytes())==d["zimage_sha256"]
    assert sha((kernel/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes())==d["dtb_sha256"]
    # Android v0 kernel payload must contain exact compiled zImage + 3g8 DTB.
    from struct import unpack_from
    page_size=unpack_from("<I",b,36)[0]
    payload_size=unpack_from("<I",b,8)[0]
    assert page_size in (2048,4096)
    assert b[page_size:page_size+payload_size] == (
        kernel/"arch/arm/boot/zImage").read_bytes() + (
        kernel/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes()
    if DEST.exists():
        assert sha(DEST.read_bytes())==EXPECTED, "Refuse to overwrite alternate BOOT"
    else:
        DEST.parent.mkdir(parents=True,exist_ok=True)
        # No existing G716 boot artifact modified; test image lives beside it.
        with DEST.open("xb") as out:
            out.write(b)
    assert sha(DEST.read_bytes())==EXPECTED
    print("G732 BUILD-ONLY G730 OTG BOOT PASS",EXPECTED)
    print("Physical BOOT untouched. OTG 5V, USB storage and recovery NOT qualified.")

if __name__=="__main__":
    main()
