#!/usr/bin/env python3
"""G731 static regression checks for SM-T311 USB Gadget HAL/EHCI SELinux.

Never communicates with tablet or toggles VBUS. Model checks intentionally
verify that host resume can avoid unnecessary EHCI power transitions.
"""
from pathlib import Path

ANDROID=Path(__file__).resolve().parents[4]
HAL=(ANDROID/"hardware/interfaces/usb/gadget/1.1/default/T311RoleWorker.cpp").read_text()
POL=ANDROID/"device/samsung/lt01-common/sepolicy/vendor"
GEN=(POL/"genfs_contexts").read_text()
ALLOW=(POL/"lt013g_usb_gadget_caps.te").read_text()

safe={
  "early_runtime_status":
    HAL.find('ReadFileToString(kEhciPmStatus, &pmStatus)') <
    HAL.find('ReadFileToString(kEhciPmControl,'),
  "no_pm_change_if_already_active":
    'if (android::base::ReadFileToString(kEhciPmStatus, &pmStatus) &&' in HAL and
    'pmStatus.find("active") == 0' in HAL,
  "explicit_auto_pm_hold_only":
    'pmControl == "auto\\n" || pmControl == "auto"' in HAL and
    'mT311EhciPmHeld.store(true)' in HAL,
  "no_stop_if_host_not_ready_but_vbus_safe_off":
    'if (!ready) {' in HAL and 'if (!verifiedOff())' in HAL,
  "no_stop_on_refused_arm_if_verified_off":
    'if (!android::base::WriteStringToFile("1\\n", kArm)) {' in HAL and
    HAL.count('if (!verifiedOff())')>=2,
  "readback_latched_boost_still_required":
    'bool verifiedOn()' in HAL and 'pmic == 1' in HAL and
    'lease == 1' in HAL,
  "no_gadget_if_still_host":
    'if (hostPresent(&clean)) return;' in HAL,
  "modem_present_before_source":
    'xmm6262Enumerated()' in HAL and
    '(hcdReady & 0x6) == 0x6' in HAL and
    'vendor == "1519\\n"' in HAL and
    'product == "0020\\n"' in HAL,
  "precise_pm_control_label":
    'genfscon sysfs /devices/platform/soc/12580000.usb/power/control u:object_r:sysfs_lt013g_otg_control:s0' in GEN,
  "precise_pm_state_label":
    'genfscon sysfs /devices/platform/soc/12580000.usb/power/runtime_status u:object_r:sysfs_lt013g_otg_diag:s0' in GEN,
  "no_broad_sysfs_permission":
    'allow hal_usb_gadget_default sysfs:file' not in ALLOW,
  "read_only_diag":
    'allow hal_usb_gadget_default sysfs_lt013g_otg_diag:file r_file_perms;' in ALLOW,
  "scoped_control":
    'allow hal_usb_gadget_default sysfs_lt013g_otg_control:file { getattr open write };' in ALLOW and
    'allow hal_usb_gadget_default sysfs_lt013g_otg_control:file read;' in ALLOW,
}
for name, ok in safe.items():
    assert ok,name
    print("PASS",name)

# Model: already ACTIVE runtime/HSIC must not request a PM write;
# idle EHCI may request a targeted runtime hold, not whole USB reset.
def need_pm_write(status,control):
    if status=="active":return False
    return control=="auto"
for status,control,expected in [
    ("active","auto",False),
    ("active","on",False),
    ("suspended","auto",True),
    ("suspended","on",False),
]:
    assert need_pm_write(status,control)==expected,(status,control)
    print("PASS status",status,control,"write",expected)
print("G731_USB_HAL_SELINUX_SOURCE_MODEL_PASS",len(safe)+4)
