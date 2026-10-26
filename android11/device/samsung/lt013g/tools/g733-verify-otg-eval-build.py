#!/usr/bin/env python3
"""G733 fail-closed ABI/product check for the *build-only* G731 OTG image.

Unlike the older G716 release gate, this selects the actual G730 ARM32
source and G731 USB Gadget HAL/SELinux output. Never authorizes flashing.
"""
import hashlib, json, struct
from pathlib import Path

ANDROID=Path(__file__).resolve().parents[4]
PORT=Path("/home/debian/Projekty/samsung/linux-7.3-port")
KERNEL=PORT/"build/g724-otg-boost-gated-host-only"
BOOT=ANDROID/"device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G730-OTG-EVAL-8MiB.img"
META=json.loads((PORT/"artifacts/G730-OTG-ARM32-TEST-ONLY/G730-OTG-ARM32-TEST-MANIFEST.json").read_text())
OUTPUT=ANDROID/"out/target/product/lt013g/system/vendor"
checks={}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def check(name,condition):
    checks[name]=bool(condition)
    print(("PASS" if checks[name] else "FAIL")+" "+name)

image=BOOT.read_bytes()
cfg=(KERNEL/".config").read_text()
check("G730_exact_eight_MiB",len(image)==8388608)
check("G730_manifest_test_only",META["stage"]=="G730" and
      META["boot_flash_authorized"] is False and
      META["complete_otg_functionality_verified"] is False)
check("G730_sha_from_manifest",sha(BOOT)==META["boot_sha256"]==
     "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b")
check("G730_armv7_kernel_name",(KERNEL/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak")
check("G730_OTG_optin_on", "CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y" in cfg)
check("G730_no_abandoned_RCU","CONFIG_PAGE_RCU_FREE=y" not in cfg and
      "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in cfg)
check("G730_EHCI_OHCI_USB_STOR",all("CONFIG_"+n+"=y" in cfg for n in (
    "USB_EHCI_EXYNOS","USB_OHCI_EXYNOS","USB_STORAGE","EXTCON_MAX77693")))
check("G730_zImage_matches",sha(KERNEL/"arch/arm/boot/zImage")==META["zimage_sha256"])
check("G730_DTB_matches",sha(KERNEL/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb")==META["dtb_sha256"])
if len(image)>=2048 and image.startswith(b"ANDROID!"):
    psz=struct.unpack_from("<I",image,36)[0]
    sz=struct.unpack_from("<I",image,8)[0]
    payload=image[psz:psz+sz]
else:payload=b""
check("G730_payload_zImage_DTB",payload==(
    KERNEL/"arch/arm/boot/zImage").read_bytes()+
    (KERNEL/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes())
expected=[
    ("HAL",OUTPUT/"bin/hw/android.hardware.usb.gadget@1.1-service",
     "fe0cc1bb9ba1caed0603f4cc8175d593af20b1cd585ea69ffaaffb785d9fa075"),
    ("vendor_CIL",OUTPUT/"etc/selinux/vendor_sepolicy.cil",
     "a3b5dc65cbe28bca151fbfa3a0998b6c4d2262bceb65cf33c5881b8a39fe5dfe"),
    ("precompiled_policy",OUTPUT/"etc/selinux/precompiled_sepolicy",
     "8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb")
]
for name,path,digest in expected:
    check("G731_compiled_"+name,path.is_file() and sha(path)==digest)
host=OUTPUT/"etc/selinux/precompiled_sepolicy.plat_sepolicy_and_mapping.sha256"
plat=ANDROID/"out/target/product/lt013g/system/etc/selinux/plat_sepolicy_and_mapping.sha256"
check("G731_plat_policy_hash_consistency",host.is_file() and plat.is_file() and host.read_bytes()==plat.read_bytes())
# Physical G727 and currently working modem are not a release authorization.
print(f"G733_OTG_BUILD_GATE={sum(checks.values())}/{len(checks)}")
print("INSTALL_AUTHORIZED=FALSE")
raise SystemExit(0 if all(checks.values()) else 1)
