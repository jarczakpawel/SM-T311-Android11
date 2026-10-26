#!/usr/bin/env python3
"""G733 source-anchored XMM6262 HSIC guard test: no device writes.

The host role must not energize external VBUS unless internal Comneon
modem has enumerated as usb1-2 1519:0020. File existence alone is
insufficient. Models the simple ID comparison, not modem data service.
"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
HAL=(ROOT/"hardware/interfaces/usb/gadget/1.1/default/T311RoleWorker.cpp").read_text()
COMMON=(ROOT/"device/samsung/lt01-common/sepolicy/vendor")
LABELS=(COMMON/"genfs_contexts").read_text()
PERMS=(COMMON/"lt013g_usb_gadget_caps.te").read_text()

source_checks={
    "modem_idVendor":'/sys/bus/usb/devices/1-2/idVendor' in HAL,
    "modem_idProduct":'/sys/bus/usb/devices/1-2/idProduct' in HAL,
    "read_both_ids":'ReadFileToString(kModemVendor, &vendor)' in HAL and
       'ReadFileToString(kModemProduct, &product)' in HAL,
    "require_vendor_1519":'vendor == "1519\\n"' in HAL,
    "require_product_0020":'product == "0020\\n"' in HAL,
    "real_identity_before_source":'muxHost == 1 && xmm6262Enumerated()' in HAL,
    "not_just_file_existence":'access(kModem, R_OK)' not in HAL,
    "ensure_EHCI_OHCI":"(hcdReady & 0x6) == 0x6" in HAL,
    "do_not_modify_active_HSIC":'pmStatus.find("active") == 0' in HAL and
        HAL.find('ReadFileToString(kEhciPmStatus, &pmStatus)') <
        HAL.find('ReadFileToString(kEhciPmControl,'),
    "vendor_identity_label":'/devices/platform/soc/12580000.usb/usb1/1-2/idVendor' in LABELS,
    "product_identity_label":'/devices/platform/soc/12580000.usb/usb1/1-2/idProduct' in LABELS,
    "HAL_label_scoped":"allow hal_usb_gadget_default sysfs_lt013g_modem_state:file r_file_perms;" in PERMS,
}
for n,result in source_checks.items():
    assert result,n
    print("PASS source",n)

def identity(vendor,product):
    return vendor in ("1519","1519\n") and product in ("0020","0020\n")
examples=[
 ("1519\n","0020\n",True),
 ("1519","0020",True),
 ("1519","",False),
 ("","0020",False),
 ("1519","0021",False),
 ("1234","0020",False),
 ("1519","0020x",False),
 ("1519","0020\n\n",False)
]
for vendor,product,expected in examples:
    assert identity(vendor,product)==expected,(vendor,product)
    print("PASS identity",repr(vendor),repr(product),"valid",expected)
print("G733_HSIC_IDENTITY_GUARD_PASS",len(source_checks)+len(examples))
